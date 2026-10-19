#!/bin/sh
# run.sh - das Spiel auf dem Automaten auf der Vektorplatine starten, vom PC aus (liegt als Kopie im Spielordner).
# Wie im Tempest-2000-Port: das Menue haelt die Platine und geht auf ein einfaches Beenden nicht weg, darum wird die
# Anmeldung auf tty1 angehalten, das Spiel laeuft, danach kommt das Menue zurueck.
#
#   ssh pi@192.168.0.122 'nohup setsid /persistent/games/vpinball/run.sh > /dev/null 2>&1 < /dev/null &'
#
# Umgebung aus dem Aufruf wird durchgereicht (VP_FRAMES=1200 beendet nach 30 s bei 40 Bildern/s, VP_TABLE, VP_AUTOPLAY ...).
# Die Bildkosten stehen laufend in run.log (VP_STATS=1). Ein zweiter Aufruf, solange einer laeuft, tut nichts.
cd "$(dirname "$0")" || exit 1
[ -f run.lock ] && exit 0
touch run.lock
sudo -n systemctl stop getty@tty1
while pgrep -x vector_pie_menu > /dev/null; do sleep 1; done
sleep 1
chmod +x pinball
VP_OUTPUT=dvg VP_STATS=1 ./pinball "$@" > run.log 2>&1
sudo -n systemctl start getty@tty1
rm -f run.lock
