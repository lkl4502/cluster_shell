FROM ubuntu:22.04

LABEL maintainer="Oh Hong Seok <lkl4502@kookmin.ac.kr>"

RUN apt update && apt install -qq -y \
gcc \
ssh \
openssh-server \
openssh-client \
vim \
sudo \
&& mkdir /var/run/sshd \
&& apt clean \
&& rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

RUN sed -ri 's/^#?PermitRootLogin\s+.*/PermitRootLogin yes/' /etc/ssh/sshd_config
RUN sed -ri 's/UsePAM yes/#UsePAM yes/g' /etc/ssh/sshd_config

EXPOSE 22

CMD ["/usr/sbin/sshd", "-D"]