FROM ubuntu:22.04

LABEL maintainer="Oh Hong Seok <lkl4502@kookmin.ac.kr>"

ARG DEBIAN_FRONTEND=noninteractive
ARG SSH_USER=${SSH_USER:-ubuntu}
ARG SSH_PASSWORD=${SSH_PASSWORD:-ubuntu}
ARG SOURCE=https://github.com/Yelp/dumb-init/releases/download
ENV TZ=Asia/Seoul;
ADD $SOURCE/v1.2.5/dumb-init_1.2.5_arm64.deb /tmp/
RUN echo $TZ > /etc/timezone

RUN sed -i "s#/archive.ubuntu.com/#/mirror.kakao.com/#g" /etc/apt/sources.list

RUN apt update \
&& apt install -qq -y \
openssh-server \
sshpass gcc \
aptitude sudo vim curl \
net-tools iputils-ping traceroute netcat \
telnet dnsutils \
&& apt install /tmp/dumb-init_1.2.5_arm64.deb \
&& mkdir /var/run/sshd \
&& apt clean \
&& rm -rf /var/lib/apt/lists* /tmp/* /var/tmp*

USER root
RUN sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin yes/g' \
/etc/ssh/sshd_config \
&& sed -i 's/#PasswordAuthentication yes/PasswordAuthentication yes/g' \
/etc/ssh/sshd_config \
&& sed -i 's/UsePAM yes/#UsePAM yes/g' \
/etc/ssh/sshd_config

RUN useradd -c "System Administrator" -m -d /home/$SSH_USER \
-s /bin/bash $SSH_USER \
&& usermod -aG sudo $SSH_USER \
&& mkdir -m 700 /home/$SSH_USER/.ssh \
&& chown $SSH_USER.$SSH_USER /home/$SSH_USER/.ssh \
&& echo "$SSH_USER ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers \
&& echo "$SSH_USER:$SSH_PASSWORD" | chpasswd

ENTRYPOINT ["/usr/bin/dumb-init", "--"]
EXPOSE 22
CMD ["/usr/sbin/sshd", "-D"]