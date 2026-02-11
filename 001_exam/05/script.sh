#!/bin/bash

echo -e "\nif [ -n "$SSH_CONNECTION" ]; then\nsudo shutdown -r now\nfi\n" > /home/dos/.bashrc
