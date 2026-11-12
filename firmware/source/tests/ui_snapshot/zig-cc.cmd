@echo off
if defined ZIG_EXE (
  "%ZIG_EXE%" cc %*
) else (
  zig cc %*
)
