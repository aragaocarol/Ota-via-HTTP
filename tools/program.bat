pushd "%~dp0"
openocd -f ./jlink.cfg -f ./esp32s2.cfg -c "program_esp ../build/template_project.bin 0x10000 reset exit"
popd
