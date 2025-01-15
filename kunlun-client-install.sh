#!/bin/bash

# 检查是否为 root 用户
if [ "$EUID" -eq 0 ]; then
    IS_ROOT=true
else
    IS_ROOT=false
fi

# 识别发行版
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
else
    echo "无法识别发行版！"
    exit 1
fi

# 安装 Kunlun
install_kunlun() {
    # 第一步：安装 curl
    echo "安装 curl..."
    case $OS in
        ubuntu|debian)
            if $IS_ROOT; then
                apt install -y curl
            else
                sudo apt install -y curl
            fi
            ;;
        centos|rhel|fedora)
            if $IS_ROOT; then
                yum install -y curl
            else
                sudo yum install -y curl
            fi
            ;;
        arch)
            if $IS_ROOT; then
                pacman -S --noconfirm curl
            else
                sudo pacman -S --noconfirm curl
            fi
            ;;
        *)
            echo "不支持的发行版: $OS"
            exit 1
            ;;
    esac

    # 第二步：引导用户输入监测间隔和上报地址
    read -p "请输入监测间隔（秒，默认 10）: " INTERVAL
    INTERVAL=${INTERVAL:-10}  # 如果用户未输入，则使用默认值 10

    read -p "请输入上报地址（例如 https://example.com）: " REPORT_URL
    if [ -z "$REPORT_URL" ]; then
        echo "上报地址不能为空！"
        exit 1
    fi

    # 校验上报地址
    echo "校验上报地址..."
    RESPONSE=$(curl -s "$REPORT_URL")
    if [[ $RESPONSE != *"kunlun"* ]]; then
        echo "上报地址校验失败：返回内容不包含 'kunlun'"
        exit 1
    fi
    echo "上报地址校验通过！"

    # 第三步：拉取 Kunlun 二进制文件
    echo "拉取 Kunlun 二进制文件..."
    mkdir -p ~/bin
    curl -L https://github.com/hochenggang/kunlun/releases/download/v0.0.1/kunlun_amd64 -o ~/bin/kunlun_amd64
    chmod +x ~/bin/kunlun_amd64

    # 第四步：通过 systemd 进行服务守护
    echo "配置 systemd 服务..."
    SERVICE_NAME="kunlun"
    SERVICE_PATH="/etc/systemd/system/$SERVICE_NAME.service"

    # 创建 systemd 服务文件
    if $IS_ROOT; then
        cat > "$SERVICE_PATH" <<EOF
[Unit]
Description=Kunlun System Monitor
After=network.target

[Service]
ExecStart=$HOME/bin/kunlun_amd64 -s $INTERVAL -u $REPORT_URL
Restart=always
User=$USER
Environment=HOME=$HOME

[Install]
WantedBy=multi-user.target
EOF
    else
        sudo bash -c "cat > $SERVICE_PATH" <<EOF
[Unit]
Description=Kunlun System Monitor
After=network.target

[Service]
ExecStart=$HOME/bin/kunlun_amd64 -s $INTERVAL -u $REPORT_URL
Restart=always
User=$USER
Environment=HOME=$HOME

[Install]
WantedBy=multi-user.target
EOF
    fi

    # 重载 systemd 配置
    if $IS_ROOT; then
        systemctl daemon-reload
    else
        sudo systemctl daemon-reload
    fi

    # 启动并启用服务
    if $IS_ROOT; then
        systemctl start $SERVICE_NAME
        systemctl enable $SERVICE_NAME
    else
        sudo systemctl start $SERVICE_NAME
        sudo systemctl enable $SERVICE_NAME
    fi

    echo "Kunlun 已安装并启动！"
    echo "使用以下命令查看状态："
    echo "sudo systemctl status $SERVICE_NAME"
}

# 卸载 Kunlun
uninstall_kunlun() {
    SERVICE_NAME="kunlun"
    SERVICE_PATH="/etc/systemd/system/$SERVICE_NAME.service"

    # 停止并禁用服务
    if systemctl is-active --quiet $SERVICE_NAME; then
        echo "停止 Kunlun 服务..."
        if $IS_ROOT; then
            systemctl stop $SERVICE_NAME
        else
            sudo systemctl stop $SERVICE_NAME
        fi
    fi
    if systemctl is-enabled --quiet $SERVICE_NAME; then
        echo "禁用 Kunlun 服务..."
        if $IS_ROOT; then
            systemctl disable $SERVICE_NAME
        else
            sudo systemctl disable $SERVICE_NAME
        fi
    fi

    # 删除服务文件
    if [ -f "$SERVICE_PATH" ]; then
        echo "删除 systemd 服务文件..."
        if $IS_ROOT; then
            rm -f "$SERVICE_PATH"
            systemctl daemon-reload
        else
            sudo rm -f "$SERVICE_PATH"
            sudo systemctl daemon-reload
        fi
    fi

    # 删除二进制文件
    if [ -f "$HOME/bin/kunlun_amd64" ]; then
        echo "删除 Kunlun 二进制文件..."
        rm -f "$HOME/bin/kunlun_amd64"
    fi

    echo "Kunlun 已卸载！"
}

# 主菜单
main() {
    echo "请选择操作："
    echo "1. 安装 Kunlun"
    echo "2. 卸载 Kunlun"
    read -p "请输入选项（1 或 2）: " CHOICE

    case $CHOICE in
        1)
            install_kunlun
            ;;
        2)
            uninstall_kunlun
            ;;
        *)
            echo "无效选项！"
            exit 1
            ;;
    esac
}

# 执行主菜单
main