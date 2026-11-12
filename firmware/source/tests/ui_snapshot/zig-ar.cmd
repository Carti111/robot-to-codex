@echo off
if defined ZIG_EXE (
  "%ZIG_EXE%" ar %*
) else (
  zig ar %*
)
