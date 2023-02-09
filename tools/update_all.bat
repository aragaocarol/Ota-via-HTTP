pushd "%~dp0"
openocd -f ./jlink.cfg -f ./esp32s2.cfg -c "program_esp ../build/bootloader/bootloader.bin 0x1000 exit"
openocd -f ./jlink.cfg -f ./esp32s2.cfg -c "program_esp ../build/partition_table/partition-table.bin 0x8000 exit"
openocd -f ./jlink.cfg -f ./esp32s2.cfg -c "program_esp ../build/template_project.bin 0x10000 exit"
openocd -f ./jlink.cfg -f ./esp32s2.cfg -c "program_esp empty.bin 0x160000 exit"
openocd -f ./jlink.cfg -f ./esp32s2.cfg -c "program_esp empty.bin 0x2B0000 reset exit"
popd
