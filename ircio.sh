#!/bin/bash
network=irc.libera.chat
./ii -i . -s "$network" -p 6667 -n makhno -f nestor 2>&1 > $network/glob
