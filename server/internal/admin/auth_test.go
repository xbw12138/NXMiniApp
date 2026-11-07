package admin

import (
	"encoding/base32"
	"testing"
	"time"
)

const testSecret = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ"

func testKey(t *testing.T) []byte {
	t.Helper()
	key, err := base32.StdEncoding.WithPadding(base32.NoPadding).DecodeString(testSecret)
	if err != nil {
		t.Fatal(err)
	}
	return key
}

func TestTOTPVector(t *testing.T) {
	// RFC 6238 附录 B：T=59 时 8 位结果为 94287082，6 位即 287082。
	if got := totpAt(testKey(t), time.Unix(59, 0)); got != "287082" {
		t.Fatalf("got %s, want 287082", got)
	}
}

func TestVerifyTOTP(t *testing.T) {
	if !verifyTOTP(testSecret, totpAt(testKey(t), time.Now())) {
		t.Fatal("当前时间窗的动态码应通过")
	}
	if verifyTOTP(testSecret, "123456") && totpAt(testKey(t), time.Now()) != "123456" {
		t.Fatal("错误动态码不应通过")
	}
	if verifyTOTP(testSecret, "abc") {
		t.Fatal("非 6 位输入不应通过")
	}
}
