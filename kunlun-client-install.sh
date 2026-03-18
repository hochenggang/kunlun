#!/bin/bash

set -e

KUNLUN_BIN="$HOME/bin/kunlun"
KUNLUN_SERVICE="kunlun.service"
KUNLUN_SERVICE_PATH="/etc/systemd/system/$KUNLUN_SERVICE"
RELEASES_BASE_URL="https://github.com/hochenggang/kunlun/releases/download"

print_info() {
    echo -e "\033[36m[INFO]\033[0m $1"
}

print_success() {
    echo -e "\033[32m[SUCCESS]\033[0m $1"
}

print_error() {
    echo -e "\033[31m[ERROR]\033[0m $1"
}

is_root() {
    [[ "$EUID" -eq 0 ]]
}

get_sudo() {
    if is_root; then
        echo ""
    else
        echo "sudo"
    fi
}

detect_arch() {
    local arch
    arch=$(uname -m)
    case "$arch" in
        x86_64|amd64)
            echo "amd64"
            ;;
        aarch64|arm64)
            echo "arm64"
            ;;
        *)
            print_error "不支持的架构: $arch"
            exit 1
            ;;
    esac
}

get_latest_version() {
    local version
    version=$(curl -sI "https://github.com/hochenggang/kunlun/releases/latest" | grep -i "location:" | sed 's/.*tag\///' | tr -d '\r\n')
    if [[ -z "$version" ]]; then
        version="v0.2.0"
    fi
    echo "$version"
}

install_package() {
    local package="$1"
    local sudo_cmd
    sudo_cmd=$(get_sudo)

    if [[ -f /etc/os-release ]]; then
        . /etc/os-release
        case "$ID" in
            ubuntu|debian)
                $sudo_cmd apt update
                $sudo_cmd apt install -y "$package"
                ;;
            centos|rhel|fedora|almalinux|rocky)
                $sudo_cmd yum install -y "$package"
                ;;
            arch|manjaro)
                $sudo_cmd pacman -S --noconfirm "$package"
                ;;
            opensuse*)
                $sudo_cmd zypper install -y "$package"
                ;;
            *)
                print_error "不支持的发行版: $ID"
                return 1
                ;;
        esac
    else
        print_error "无法识别发行版"
        return 1
    fi
}

install_kunlun() {
    print_info "开始安装 Kunlun..."

    if ! command -v curl &> /dev/null; then
        print_info "正在安装 curl..."
        install_package curl || exit 1
    fi

    local arch
    arch=$(detect_arch)
    print_info "检测到架构: $arch"

    local version
    version=$(get_latest_version)
    print_info "最新版本: $version"

    local download_url="$RELEASES_BASE_URL/$version/kunlun-client-linux-$arch"

    read -p "请输入上报地址: " REPORT_URL
    if [[ -z "$REPORT_URL" ]]; then
        print_error "上报地址不能为空"
        exit 1
    fi

    print_info "验证上报地址..."
    local response
    response=$(curl -s "$REPORT_URL" 2>/dev/null || true)
    if [[ "$response" != *"kunlun"* ]]; then
        print_error "上报地址验证失败：GET 请求返回内容不包含 'kunlun'"
        exit 1
    fi
    print_success "上报地址验证通过"

    print_info "正在下载 Kunlun ($arch)..."
    mkdir -p "$HOME/bin"
    if ! curl -fL "$download_url" -o "$KUNLUN_BIN" 2>/dev/null; then
        print_error "下载失败: $download_url"
        exit 1
    fi
    chmod +x "$KUNLUN_BIN"
    print_success "下载完成"

    local sudo_cmd
    sudo_cmd=$(get_sudo)

    print_info "配置 systemd 服务..."
    $sudo_cmd tee "$KUNLUN_SERVICE_PATH" > /dev/null <<EOF
[Unit]
Description=Kunlun System Monitor
After=network.target

[Service]
ExecStart="$KUNLUN_BIN" -u "$REPORT_URL"
Restart=always
RestartSec=5
User=$USER
Environment=HOME=$HOME

[Install]
WantedBy=multi-user.target
EOF

    print_info "启动服务..."
    $sudo_cmd systemctl daemon-reload
    $sudo_cmd systemctl enable "$KUNLUN_SERVICE"
    $sudo_cmd systemctl restart "$KUNLUN_SERVICE"

    print_success "Kunlun 安装完成！"
    echo ""
    echo "查看状态: systemctl status $KUNLUN_SERVICE"
    echo "查看日志: journalctl -u $KUNLUN_SERVICE -f"
}

uninstall_kunlun() {
    print_info "正在卸载 Kunlun..."

    local sudo_cmd
    sudo_cmd=$(get_sudo)

    if systemctl is-active "$KUNLUN_SERVICE" &>/dev/null; then
        $sudo_cmd systemctl stop "$KUNLUN_SERVICE"
    fi

    if systemctl is-enabled "$KUNLUN_SERVICE" &>/dev/null; then
        $sudo_cmd systemctl disable "$KUNLUN_SERVICE"
    fi

    $sudo_cmd rm -f "$KUNLUN_SERVICE_PATH"
    rm -f "$KUNLUN_BIN"
    $sudo_cmd systemctl daemon-reload

    print_success "Kunlun 已卸载"
}

show_menu() {
    echo ""
    echo "================================"
    echo "       Kunlun 管理工具"
    echo "================================"
    echo "1. 安装 Kunlun"
    echo "2. 卸载 Kunlun"
    echo "3. 退出"
    echo "================================"
}

main() {
    show_menu
    read -p "请选择 (1-3): " choice

    case "$choice" in
        1)
            install_kunlun || exit 1
            ;;
        2)
            uninstall_kunlun
            ;;
        3)
            exit 0
            ;;
        *)
            print_error "无效选项"
            exit 1
            ;;
    esac
}

main
