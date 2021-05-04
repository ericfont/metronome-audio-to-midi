#!/bin/bash

wget https://github.com/jackaudio/jack2-releases/releases/download/v1.9.18/jack2-macOS-universal-v1.9.18.tar.gz
tar -xf jack2-macOS-universal-v1.9.18.tar.gz
sudo installer -pkg jack2-osx-1.9.18.pkg -target /
