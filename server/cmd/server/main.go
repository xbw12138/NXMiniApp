package main

import (
	"flag"
	"log"

	"nxminiapp/server/internal/admin"
	"nxminiapp/server/internal/api"
	"nxminiapp/server/internal/config"
	"nxminiapp/server/internal/database"
	"nxminiapp/server/internal/docs"
	"nxminiapp/server/internal/miniappstore"
)

func main() {
	genAdmin := flag.Bool("gen-admin", false, "生成后台登录凭据（密码 + TOTP 密钥）后退出")
	genPassword := flag.String("password", "", "配合 -gen-admin 使用：指定后台密码，为空则随机生成")
	flag.Parse()
	if *genAdmin {
		admin.PrintCredentials(*genPassword)
		return
	}

	cfg := config.Load()
	if err := cfg.EnsureDirs(); err != nil {
		log.Fatalf("创建数据目录失败: %v", err)
	}

	db, err := database.Connect(cfg.DBDSN)
	if err != nil {
		log.Fatalf("数据库连接失败: %v", err)
	}
	if err := database.Migrate(db); err != nil {
		log.Fatalf("数据库迁移失败: %v", err)
	}
	if err := miniappstore.Seed(db, cfg.DataDir, cfg.SeedDir); err != nil {
		log.Printf("种子数据导入失败: %v", err)
	}
	if err := docs.Seed(db, cfg.DocsDir); err != nil {
		log.Printf("文档种子导入失败: %v", err)
	}

	router, env := api.NewRouter(cfg, db)
	admin.Register(router, env)

	log.Printf("NX Store server listening on :8080 (data dir: %s)", cfg.DataDir)
	if err := router.Run(":8080"); err != nil {
		log.Fatalf("服务启动失败: %v", err)
	}
}
