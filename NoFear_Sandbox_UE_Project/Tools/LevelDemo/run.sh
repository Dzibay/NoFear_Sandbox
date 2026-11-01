#!/bin/bash
# run.sh <сценарий.py> — прогон сценария на L_Demo в -game без окна; печатает строки NFT из лога.
source "$(dirname "$0")/env.sh"
S="$1"; L="$LOGS/${S%.py}.log"
timeout 290 "$UE/Engine/Binaries/Win64/UnrealEditor.exe" "$U" L_Demo -game -nullrhi -unattended -nosplash -EnablePlugins=PythonScriptPlugin -abslog="$L" -ExecCmds="py $T/$S"; echo exit=$?
grep -a "NFT \|Python: Error" "$L" | sed 's/^.*NFT //'
