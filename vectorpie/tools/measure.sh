#!/bin/sh
# measure.sh - drei Messlaeufe auf der Vektorplatine des Automaten, nacheinander, ohne dass das Menue dazwischen zurueckkommt:
# Tisch 8 und Tisch 7 mit Automat (je 30 s), dann der Attract (30 s). Ergebnis: measure_<name>.log im Spielordner, die
# letzten Zeilen sind die Zaehler des SDK (Striche, Weglaengen). Aufruf wie run.sh ueber nohup setsid.
cd "$(dirname "$0")" || exit 1
[ -f run.lock ] && exit 0
touch run.lock
sudo -n systemctl stop getty@tty1
while pgrep -x vector_pie_menu > /dev/null; do sleep 1; done
sleep 1
chmod +x pinball
export VP_OUTPUT=dvg VP_STATS=1 VP_NOSAVE=1 VP_FRAMES=1200
VP_TABLE=8 VP_AUTOPLAY=1 ./pinball > measure_t8.log 2>&1
VP_TABLE=7 VP_AUTOPLAY=1 ./pinball > measure_t7.log 2>&1
./pinball > measure_attract.log 2>&1
sudo -n systemctl start getty@tty1
rm -f run.lock
touch measure.done
