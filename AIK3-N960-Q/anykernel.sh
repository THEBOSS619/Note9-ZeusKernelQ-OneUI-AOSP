# AnyKernel3 Ramdisk Mod Script
# osm0sis @ xda-developers

## AnyKernel setup
# begin properties
properties() { '
kernel.string=Zeus Kernel for Android Q
do.devicecheck=1
do.modules=0
do.systemless=1
do.cleanup=1
do.cleanuponabort=0
device.name1=
device.name2=
device.name3=
device.name4=
device.name5=crownlte
device.name6=crownltexx
supported.versions=10
supported.patchlevels=
'; } # end properties

# shell variables
block=/dev/block/platform/11120000.ufs/by-name/BOOT;
is_slot_device=auto;
ramdisk_compression=auto;


## AnyKernel methods (DO NOT CHANGE)
# import patching functions/variables - see for reference
. tools/ak3-core.sh;


## AnyKernel file attributes
# set permissions/ownership for included ramdisk files
set_perm_recursive 0 0 755 644 $ramdisk/*;
set_perm_recursive 0 0 750 750 $ramdisk/init* $ramdisk/sbin;


## AnyKernel install
dump_boot;

ui_print ""
ui_print "|====================================|"
ui_print "|          .,c:;'.                   |"                       
ui_print "|         .dWMMWNK0OOOOOOOko'        |"                       
ui_print "|         cNMMMMMMMMMMMMMMMM0'       |"                       
ui_print "|        '0MMMMMMMMMMMMMMMMMX;       |"                       
ui_print "|       .dNWWWWMMMMMMMMMMMMMN:       |"                       
ui_print "|  ;oxkkl',,,,,;;::cclodxO0XX:       |"                       
ui_print "| .kWMMWx;,''....         ..;cc;..   |"                       
ui_print "|   .;oOKNWWWNNXK00OOkxddolc;;kMWN0o.|"                       
ui_print "|       .;xkdkOKXNWMMMMMMMMMMWWMMMNk'|"                       
ui_print "|         '.   ..',;::clooddddolc;'  |"                       
ui_print "|                                    |"                                 
ui_print "|                                    |"                                 
ui_print "|                                    |"                                 
ui_print "|               .....                |"                       
ui_print "|              .......               |"                       
ui_print "|         '.             '.          |"                       
ui_print "|         ;x;          .cd'          |"                       
ui_print "|          lXkc'    .,lOKc           |"                       
ui_print "|          .dNWk.   '0WXl            |"                      
ui_print "|           .dO,     :0l             |"                       
ui_print "|            ..       .              |"                       
ui_print "|====================================|"    
ui_print "++++++++++++++++++++++++++++++++++++++"                              
ui_print "+            Zeus Kernel Q           +"
ui_print "+          ________________          +"
ui_print "+          |~MaintainedBy~|          +"
ui_print "+            [THEBOSS619]            +"
ui_print "+          ________________          +"
ui_print "+          |~Co-OperateBy~|          +"
ui_print "+      ~Zeus Members Believers~      +"
ui_print "++++++++++++++++++++++++++++++++++++++"
ui_print " "

ui_print "Remounting /vendor";
mount -o remount,rw /vendor;

if [ ! -e /vendor/etc/fstab.samsungexynos9810~ ]; then
	ui_print "Backing up vendor fstab";
	backup_file /vendor/etc/fstab.samsungexynos9810;
fi;

if [ ! -e /vendor/etc/init/pa_daemon_kinibi.rc~ ]; then
	backup_file /vendor/etc/init/pa_daemon_kinibi.rc;
fi;

if [ ! -e /vendor/etc/init/secure_storage_daemon_kinibi.rc~ ]; then
	backup_file /vendor/etc/init/secure_storage_daemon_kinibi.rc;
fi;

ui_print ""
ui_print "Removing mcRegistry..."
ui_print ""

rm -f /vendor/app/mcRegistry/ffffffffd0000000000000000000000a.tlbin;
rm -f /vendor/app/mcRegistry/ffffffffd00000000000000000000062.tlbin;

ui_print "Patching fstab for F2FS Support";

cp -af /tmp/anykernel/ramdisk/fstab.samsungexynos9810 /vendor/etc/;
chmod 644 /vendor/etc/fstab.samsungexynos9810;
chcon u:object_r:vendor_configs_file:s0 /vendor/etc/fstab.samsungexynos9810;

ui_print "Copying/Patching vendor script";
cp -f $home/vendor/etc/init/init.services.rc /vendor/etc/init;
cp -f $home/vendor/etc/init/pa_daemon_kinibi.rc /vendor/etc/init;
cp -f $home/vendor/etc/init/secure_storage_daemon_kinibi.rc /vendor/etc/init;

mv -f $home/dtb.img $split_img/extra;

ui_print " "
ui_print "Kernel flashing finished..."
ui_print "Welcome to Zeus world "
ui_print "Being Best kernel is not an option"
ui_print "But it is a necessity... Enjoy "

write_boot;
## end install

