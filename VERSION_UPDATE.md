# 版本更新指南

## 修改版本号的位置
每次发布新版本时，请修改以下 3 个文件：

1. **src/common/AppConfig.h**
   - 修改 `applicationVersion()` 函数返回值
   - 例如：`return QStringLiteral("1.0.3");`

2. **CMakeLists.txt**
   - 修改 `CPACK_PACKAGE_VERSION` 变量
   - 例如：`set(CPACK_PACKAGE_VERSION "1.0.3")`

3. **version.json** (部署到服务器)
   - 修改 `version` 字段
   - 修改 `url` 字段指向新的安装包
   - 示例：
     ```json
     {
         "version": "1.0.3",
         "url": "http://115.159.43.237/download/IrulerDeskPro-1.0.3-win64.exe",
         ...
     }
     ```

## 服务器环境配置 (Nginx)

如果服务器尚未配置 Web 服务，导致更新检查失败（Connection closed），请按照以下步骤安装和配置 Nginx。

### 1. 下载与安装 Nginx

**Windows 环境：**
1. 访问 https://nginx.org/en/download.html 下载稳定版 zip
2. 解压到服务器目录（例如 `C:\nginx`）
3. 运行 `nginx.exe` 启动

**Linux 环境 (Ubuntu/CentOS)：**
```bash
# Ubuntu
sudo apt update
sudo apt install nginx

# CentOS
sudo yum install nginx
```

### 2. 配置 Nginx
编辑 Nginx 配置文件（Windows: `conf/nginx.conf`, Linux 推荐：`/etc/nginx/sites-available/` 下新增站点文件并启用）。

**Linux（Ubuntu）推荐配置：**
1. 新建站点文件：
   ```bash
   sudo nano /etc/nginx/sites-available/irulerdeskpro
   ```
2. 写入以下内容（示例把文件放在 `/var/www/html/download/`）：
   ```nginx
   server {
       listen 80;
       server_name 115.159.43.237;

       charset utf-8;

       location /download/ {
           root /var/www/html;
           autoindex off;
       }
   }
   ```
3. 启用站点：
   ```bash
   sudo ln -s /etc/nginx/sites-available/irulerdeskpro /etc/nginx/sites-enabled/irulerdeskpro
   ```

**Windows 配置要点：**
- `location /download/ { root D:/server_files; }`
- 如果文件实际在 `D:\server_files\download\version.json`，则上面的 `root` 就是 `D:/server_files`

### 3. 部署更新文件
1. 创建目录 `C:\server_files\download` (Windows) 或 `/var/www/html/download` (Linux)。
2. 将以下文件放入该目录：
   - `version.json`
   - `IrulerDeskPro-1.0.3-win64.exe` (确保文件名与 version.json 中的 url 一致)
3. 注意：`version.json` 是纯 JSON，字段值不要带反引号、空格或 Markdown 格式。例如：
   ```json
   {
     "url": "http://115.159.43.237/download/IrulerDeskPro-1.0.3-win64.exe"
   }
   ```

### 4. 重启 Nginx 并验证
**Windows:**
```cmd
cd C:\nginx
nginx -s reload
```

**Linux:**
```bash
sudo nginx -t
sudo systemctl restart nginx
```

**验证方法：**
1. 在服务器本机验证（排除外网/防火墙问题）：
   ```bash
   curl -i http://127.0.0.1/download/version.json
   ```
2. 在外网机器验证（例如 Windows PowerShell）：
   ```powershell
   curl.exe -i "http://115.159.43.237/download/version.json"
   ```
3. 说明：部分云服务器公网 IP 走 NAT，服务器本机访问自己的公网 IP 可能失败，这种情况下以 `127.0.0.1` 与外网机器访问结果为准。
4. 如果外网访问仍失败，检查云安全组与防火墙放行 80/tcp：
   ```bash
   sudo ss -ltnp | grep ':80'
   sudo ufw status
   sudo ufw allow 80/tcp
   ```
