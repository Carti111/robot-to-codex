@echo off
if defined ZIG_EXE (
  "%ZIG_EXE%" ranlib %*
) else (
  zig ranlib %*
)
