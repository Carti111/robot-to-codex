@echo off
if defined ZIG_EXE (
  "%ZIG_EXE%" c++ %*
) else (
  zig c++ %*
)
