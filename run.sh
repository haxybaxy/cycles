export CYCLES_PORT=50097

cat<<EOF> config.yaml
gameHeight: 500
gameWidth: 500
gameBannerHeight: 100
gridHeight: 100
gridWidth: 100
maxClients: 60
enablePostProcessing: false
EOF

./build/bin/server &
sleep 1

# Spawn your bot
./build/bin/client zaid &

# Spawn 7 random bots
for i in {1..7}
do
./build/bin/client randomio$i &
done
