git clone https://gitlab.freedesktop.org/pulseaudio/pulseaudio.git pulseaudio
cd pulseaudio
./autogen.sh
./configure
cd src
make
cp ../../pactl.c utils/pactl.c
