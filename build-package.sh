#!/bin/bash

# Создание структуры пакета
mkdir -p pkg/usr/local/bin
mkdir -p pkg/etc/systemd/system
mkdir -p pkg/etc/async-server

# Копирование файлов
cp async_server pkg/usr/local/bin/
cp async-server.service pkg/etc/systemd/system/

# Создание контрольного файла для пакета
mkdir -p pkg/DEBIAN
cat > pkg/DEBIAN/control << EOF
Package: async-server
Version: 1.0-1
Section: net
Priority: optional
Architecture: amd64
Depends: libc6 (>= 2.28), systemd
Maintainer: Your Name <your.email@example.com>
Description: Asynchronous TCP/UDP Server with epoll
 High-performance asynchronous server handling both TCP and UDP
 connections using Linux epoll mechanism.
EOF

# Создание скриптов установки/удаления
cat > pkg/DEBIAN/postinst << EOF
#!/bin/bash
systemctl daemon-reload
EOF

cat > pkg/DEBIAN/prerm << EOF
#!/bin/bash
systemctl stop async-server 2>/dev/null || true
EOF

chmod 755 pkg/DEBIAN/postinst pkg/DEBIAN/prerm

# Сборка пакета
dpkg-deb --build pkg async-server_1.0-1_amd64.deb

# Очистка
rm -rf pkg

echo "Package built: async-server_1.0-1_amd64.deb"