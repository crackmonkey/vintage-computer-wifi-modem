echo 'const char MAIN_page[] PROGMEM = R"=====(' > ../firmware/theoldnet_serial_wifi_modem/webserver.h
cat index.html >> ../firmware/theoldnet_serial_wifi_modem/webserver.h
echo '' >> ../firmware/theoldnet_serial_wifi_modem/webserver.h
echo ')=====";' >> ../firmware/theoldnet_serial_wifi_modem/webserver.h