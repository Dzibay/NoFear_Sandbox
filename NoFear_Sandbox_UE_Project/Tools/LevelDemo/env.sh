# Общие пути для rebuild.sh и run.sh. Переопределяются переменными окружения.
# Проект открываем через ASCII-ссылку C:\NoFear_Sandbox: движок и UBT ломаются на кириллице в пути.
P="${NOFEAR_PROJECT:-C:/NoFear_Sandbox/NoFear_Sandbox_UE_Project}"
UE="${UE_ROOT:-/d/Epic Games/UE_5.8}"
U="$P/NoFear_Sandbox_UE_Project.uproject"
T="$P/Tools/LevelDemo"
LOGS="$P/Saved/LevelDemo"
mkdir -p "$LOGS"
