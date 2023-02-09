pushd "%~dp0"
openocd -f ./jlink.cfg -f ./esp32s2.cfg -c "init" -c "reset run" -c "exit"
popd
