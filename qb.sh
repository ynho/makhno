#!/bin/bash
echo "/PRIVMSG nickserv :identify makhno $1" > irc.libera.chat/in && sleep 3 && ./quotebot
