#!/bin/bash

# This script will compile and install ViChRR (Virtual Choir Rehearsal Room) on macOS.
# Written by Lukáš Ondráček.

set -e

# Install HomeBrew (+ Xcode Command Line Tools)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install.sh)"

# Install PortAudio library
brew install portaudio

# Clone git repository of ViChRR to home directory
cd
git clone 'https://github.com/ondracek-lukas/vichrr'

# Compile it
cd vichrr
make client

# Create a script for updating
cat > update.sh << EOF
#!/bin/bash
cd vichrr
make clean
git pull
make client
EOF
chmod +x update.sh

# Create link on desktop
cd ../Desktop
ln -s ../vichrr/client "ViChRR"
cd ..

# Inform user about probable success
echo
echo "Virtual Choir Rehearsal Room seems to be installed successfully on desktop."
echo "For future updating use the following command:"
echo "  vichrr/update.sh"
echo
