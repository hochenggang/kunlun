#!/bin/bash

# 常量定义
KUNLUN_BIN="$HOME/bin/kunlun"
KUNLUN_SERVICE="kunlun.service"
KUNLUN_SERVICE_PATH="/etc/systemd/system/$KUNLUN_SERVICE"
KUNLUN_URL="https://github.com/hochenggang/kunlun/releases/download/v0.1.0/kunlun_amd64"

# 函数：检查 root 权限
is_root() {
  [[ "$EUID" -eq 0 ]]
}

# 函数：安装软件包（根据发行版）
install_package() {
  local package="$1"
  local cmd

  if is_root; then
    cmd=""
  else
    cmd="sudo"
  fi

  if [[ -f /etc/os-release ]]; then
    . /etc/os-release
    case "$ID" in
      ubuntu|debian)
        $cmd apt update
        $cmd apt install -y "$package"
        ;;
      centos|rhel|fedora)
        $cmd yum install -y "$package"
        ;;
      arch)
        $cmd pacman -S --noconfirm "$package"
        ;;
      *)
        echo "不支持的发行版: $ID"
        return 1
        ;;
    esac
  else
    echo "无法识别发行版！"
    return 1
  fi
}

# 函数：安装 Kunlun
install_kunlun() {
  # 安装 curl
  echo "安装 curl..."
  if ! install_package curl; then
    return 1
  fi

  # 获取用户输入

  read -p "请输入上报地址: " REPORT_URL
  if [ -z "$REPORT_URL" ]; then
    echo "上报地址不能为空！"
    return 1
  fi

  # 校验上报地址 (未改进，保留原方法)
  echo "校验上报地址..."
  RESPONSE=$(curl -s "$REPORT_URL")
  if [[ $RESPONSE != *"kunlun"* ]]; then
    echo "上报地址校验失败：返回内容不包含 'kunlun'"
    return 1
  fi
  echo "上报地址校验通过！"

  # 下载 Kunlun 二进制文件
  echo "下载 Kunlun 二进制文件..."
  mkdir -p "$HOME/bin"
  curl -L "$KUNLUN_URL" -o "$KUNLUN_BIN"
    if [[ ! -f "$KUNLUN_BIN" ]]; then
      echo "下载 Kunlun 二进制文件失败！"
      return 1
  fi
  chmod +x "$KUNLUN_BIN"

  # 创建 systemd 服务文件 (简化)
  echo "配置 systemd 服务..."
  if is_root; then
    user=""
  else
    user="sudo"
  fi
  $user tee "$KUNLUN_SERVICE_PATH" > /dev/null <<EOF
[Unit]
Description=Kunlun System Monitor
After=network.target

[Service]
ExecStart="$KUNLUN_BIN" -u "$REPORT_URL"
Restart=always
User=$USER
Environment=HOME=$HOME

[Install]
WantedBy=multi-user.target
EOF

  # 启动并启用服务 (合并命令)
  echo "启动并启用 Kunlun 服务..."
  $user systemctl daemon-reload
  $user systemctl enable "$KUNLUN_SERVICE"
  $user systemctl restart "$KUNLUN_SERVICE" # 使用 restart 确保配置生效

  echo "Kunlun 已安装并启动！"
  echo "使用以下命令查看状态："
  echo "sudo systemctl status $KUNLUN_SERVICE"
}

# 函数：卸载 Kunlun
uninstall_kunlun() {
  echo "停止并禁用 Kunlun 服务..."
  if is_root; then
    user=""
  else
    user="sudo"
  fi
  $user systemctl stop "$KUNLUN_SERVICE"
  $user systemctl disable "$KUNLUN_SERVICE"

  echo "删除相关文件..."
  $user rm -f "$KUNLUN_SERVICE_PATH" "$KUNLUN_BIN"
  $user systemctl daemon-reload # 重载配置

  echo "Kunlun 已卸载！"
}

# 主函数
main() {
  
    echo "请选择操作："
    echo "1. 安装 Kunlun"
    echo "2. 卸载 Kunlun"
    echo "3. 退出"
    read -p "请输入选项（1-3）: " CHOICE

    case "$CHOICE" in
      1) install_kunlun || exit 1 ;; # 错误处理
      2) uninstall_kunlun ;;
      3) exit 0 ;;
      *) echo "无效选项！" ;;
    esac
}

main