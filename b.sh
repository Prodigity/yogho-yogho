#!/bin/sh
# Desktop build wrapper usable from bash.
powershell -NoProfile -Command "& 'C:\Claude\Yogho yogho\port\build.bat'" 2>&1 | grep -v "^\[" | grep -v "^$" | tail -20
