package admin

import (
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha1"
	"crypto/subtle"
	"encoding/base32"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"net/http"
	"strings"
	"sync"
	"time"

	"github.com/gin-gonic/gin"
	"golang.org/x/crypto/bcrypt"

	"nxminiapp/server/internal/api"
)

const (
	sessionCookie = "nxkit_admin_session"
	sessionTTL    = 7 * 24 * time.Hour
	challengeTTL  = 2 * time.Minute
	maxAttempts   = 5
	attemptWindow = 5 * time.Minute
)

// authStore 保存登录会话与两步验证的中间凭证，进程内即可（单管理后台）。
type authStore struct {
	mu         sync.Mutex
	sessions   map[string]time.Time
	challenges map[string]time.Time
	fails      map[string][]time.Time
}

var authState = &authStore{
	sessions:   map[string]time.Time{},
	challenges: map[string]time.Time{},
	fails:      map[string][]time.Time{},
}

func randomHex(n int) string {
	buf := make([]byte, n)
	if _, err := rand.Read(buf); err != nil {
		panic(err)
	}
	return hex.EncodeToString(buf)
}

// blocked 判断来源是否已超过失败次数上限（同时清理过期记录）。
func (s *authStore) blocked(key string) bool {
	now := time.Now()
	s.mu.Lock()
	defer s.mu.Unlock()
	kept := make([]time.Time, 0, len(s.fails[key]))
	for _, t := range s.fails[key] {
		if now.Sub(t) < attemptWindow {
			kept = append(kept, t)
		}
	}
	s.fails[key] = kept
	return len(kept) >= maxAttempts
}

func (s *authStore) noteFail(key string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.fails[key] = append(s.fails[key], time.Now())
}

func (s *authStore) newChallenge() string {
	id := randomHex(16)
	s.mu.Lock()
	defer s.mu.Unlock()
	s.challenges[id] = time.Now().Add(challengeTTL)
	return id
}

func (s *authStore) takeChallenge(id string) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	exp, ok := s.challenges[id]
	if !ok || time.Now().After(exp) {
		delete(s.challenges, id)
		return false
	}
	delete(s.challenges, id)
	return true
}

func (s *authStore) newSession() string {
	id := randomHex(24)
	s.mu.Lock()
	defer s.mu.Unlock()
	s.sessions[id] = time.Now().Add(sessionTTL)
	return id
}

func (s *authStore) validSession(id string) bool {
	if id == "" {
		return false
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	exp, ok := s.sessions[id]
	if !ok {
		return false
	}
	if time.Now().After(exp) {
		delete(s.sessions, id)
		return false
	}
	return true
}

func (s *authStore) dropSession(id string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	delete(s.sessions, id)
}

// verifyTOTP 校验 6 位动态码，允许前后各一个时间窗（时钟偏差）。
func verifyTOTP(secret, code string) bool {
	code = strings.TrimSpace(code)
	if len(code) != 6 {
		return false
	}
	normalized := strings.ToUpper(strings.ReplaceAll(strings.TrimSpace(secret), " ", ""))
	key, err := base32.StdEncoding.WithPadding(base32.NoPadding).DecodeString(normalized)
	if err != nil {
		return false
	}
	now := time.Now()
	for _, offset := range []int64{0, -1, 1} {
		want := totpAt(key, now.Add(time.Duration(offset)*30*time.Second))
		if subtle.ConstantTimeCompare([]byte(want), []byte(code)) == 1 {
			return true
		}
	}
	return false
}

func totpAt(key []byte, t time.Time) string {
	var buf [8]byte
	binary.BigEndian.PutUint64(buf[:], uint64(t.Unix()/30))
	mac := hmac.New(sha1.New, key)
	mac.Write(buf[:])
	sum := mac.Sum(nil)
	offset := sum[len(sum)-1] & 0x0f
	value := (uint32(sum[offset])&0x7f)<<24 |
		uint32(sum[offset+1])<<16 |
		uint32(sum[offset+2])<<8 |
		uint32(sum[offset+3])
	return fmt.Sprintf("%06d", value%1000000)
}

func isHTTPS(c *gin.Context) bool {
	return c.Request.TLS != nil || strings.EqualFold(c.GetHeader("X-Forwarded-Proto"), "https")
}

func setSessionCookie(c *gin.Context, id string) {
	c.SetSameSite(http.SameSiteLaxMode)
	c.SetCookie(sessionCookie, id, int(sessionTTL.Seconds()), "/", "", isHTTPS(c), true)
}

// passwordLogin 第一步：校验用户名 + 密码，通过后下发短期凭证。
func passwordLogin(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		if e.Cfg.AdminPasswordHash == "" {
			api.Abort(c, http.StatusNotImplemented, "未配置密码登录，请先设置 NXKIT_ADMIN_PASSWORD_HASH")
			return
		}
		ip := c.ClientIP()
		if authState.blocked(ip) {
			api.Abort(c, http.StatusTooManyRequests, "尝试次数过多，请 5 分钟后再试")
			return
		}
		var body struct {
			Username string `json:"username"`
			Password string `json:"password"`
		}
		if err := c.ShouldBindJSON(&body); err != nil {
			api.Abort(c, http.StatusBadRequest, "请求体无效")
			return
		}
		user := strings.TrimSpace(body.Username)
		if user == "" {
			user = "admin"
		}
		userOK := subtle.ConstantTimeCompare([]byte(user), []byte(e.Cfg.AdminUser)) == 1
		passOK := bcrypt.CompareHashAndPassword([]byte(e.Cfg.AdminPasswordHash), []byte(body.Password)) == nil
		if !userOK || !passOK {
			authState.noteFail(ip)
			api.Abort(c, http.StatusUnauthorized, "用户名或密码错误")
			return
		}
		c.JSON(http.StatusOK, gin.H{"challenge": authState.newChallenge(), "expires_in": int(challengeTTL.Seconds())})
	}
}

// totpLogin 第二步：校验动态码，通过后建立会话。
func totpLogin(e *api.Env) gin.HandlerFunc {
	return func(c *gin.Context) {
		if e.Cfg.AdminTOTPSecret == "" {
			api.Abort(c, http.StatusNotImplemented, "未配置 TOTP，请先设置 NXKIT_ADMIN_TOTP_SECRET")
			return
		}
		ip := c.ClientIP()
		if authState.blocked(ip) {
			api.Abort(c, http.StatusTooManyRequests, "尝试次数过多，请 5 分钟后再试")
			return
		}
		var body struct {
			Challenge string `json:"challenge"`
			Code      string `json:"code"`
		}
		if err := c.ShouldBindJSON(&body); err != nil {
			api.Abort(c, http.StatusBadRequest, "请求体无效")
			return
		}
		if !authState.takeChallenge(body.Challenge) {
			api.Abort(c, http.StatusUnauthorized, "验证已过期，请重新输入密码")
			return
		}
		if !verifyTOTP(e.Cfg.AdminTOTPSecret, body.Code) {
			authState.noteFail(ip)
			api.Abort(c, http.StatusUnauthorized, "动态码不正确")
			return
		}
		setSessionCookie(c, authState.newSession())
		c.JSON(http.StatusOK, gin.H{"ok": true})
	}
}

func logout(c *gin.Context) {
	if id, err := c.Cookie(sessionCookie); err == nil {
		authState.dropSession(id)
	}
	c.SetSameSite(http.SameSiteLaxMode)
	c.SetCookie(sessionCookie, "", -1, "/", "", isHTTPS(c), true)
	c.JSON(http.StatusOK, gin.H{"ok": true})
}

func sessionStatus(c *gin.Context) {
	c.JSON(http.StatusOK, gin.H{"ok": true})
}

// cookieValid 判断当前请求是否带有有效的会话 Cookie。
func cookieValid(c *gin.Context) bool {
	id, err := c.Cookie(sessionCookie)
	if err != nil {
		return false
	}
	return authState.validSession(id)
}

// PrintCredentials 生成一套登录凭据（密码、bcrypt 哈希、TOTP 密钥），供 -gen-admin 使用。
// password 为空时随机生成；指定时可用于把后台密码改成固定值。
func PrintCredentials(password string) {
	if password == "" {
		password = randomPassword()
	}
	hash, err := bcrypt.GenerateFromPassword([]byte(password), bcrypt.DefaultCost)
	if err != nil {
		panic(err)
	}
	raw := make([]byte, 20)
	if _, err := rand.Read(raw); err != nil {
		panic(err)
	}
	secret := base32.StdEncoding.WithPadding(base32.NoPadding).EncodeToString(raw)

	fmt.Println("# 追加到服务器 /root/NXMiniApp/.env（哈希用单引号包住，避免 $ 被 shell 展开）")
	fmt.Println("NXKIT_ADMIN_USER=admin")
	fmt.Printf("NXKIT_ADMIN_PASSWORD_HASH='%s'\n", hash)
	fmt.Printf("NXKIT_ADMIN_TOTP_SECRET=%s\n\n", secret)
	fmt.Printf("# 登录密码: %s\n", password)
	fmt.Printf("# 在 2FAS 中手动添加，或把下面链接生成二维码后扫描:\n")
	fmt.Printf("otpauth://totp/NXMiniApp:admin?secret=%s&issuer=NXMiniApp\n", secret)
}

func randomPassword() string {
	const alphabet = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789"
	buf := make([]byte, 16)
	rand.Read(buf)
	out := make([]byte, len(buf))
	for i, b := range buf {
		out[i] = alphabet[int(b)%len(alphabet)]
	}
	return string(out)
}
