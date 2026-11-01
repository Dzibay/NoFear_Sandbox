#!/bin/bash
# Генерация /Game/Levels/L_Demo из build_demo.py и запекание навмеша. Редактор должен быть закрыт.
source "$(dirname "$0")/env.sh"
C="$UE/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
"$C" "$U" -run=pythonscript -script="$T/build_demo.py" -EnablePlugins=PythonScriptPlugin -unattended -nullrhi -nosplash -nop4 -abslog="$LOGS/build.log" >/dev/null 2>&1; echo gen=$?
grep -a "problems\|SAVED" "$LOGS/build.log" | sed 's/^.*NFB //' | tail -2
# Без этого -ini ResavePackages ждёт асинхронную загрузку и не строит навмеш.
"$C" "$U" -run=ResavePackages -BuildNavigationData -MAP=L_Demo "-ini:Engine:[/Script/NavigationSystem.NavigationSystemV1]:bWaitForAsyncLoadingBeforeBuildingNavigationAutomatically=False" -unattended -nullrhi -nosplash -nop4 -abslog="$LOGS/nav.log" >/dev/null 2>&1; echo nav=$?
grep -a "Success - " "$LOGS/nav.log" | tail -1
