LEDS_MENU:=LED modules

define KernelPackage/leds-nu801
  SUBMENU:=$(LEDS_MENU)
  TITLE:=Meraki MR18 LED support
  DEPENDS:=@TARGET_ath79
  KCONFIG:=CONFIG_LEDS_NU801=m
  FILES:=$(LINUX_DIR)/drivers/leds/leds-nu801.ko
  AUTOLOAD:=$(call AutoLoad,60,leds-nu801,1)
endef

define KernelPackage/leds-nu801/description
 Kernel module for the nu801 LED driver used on the Meraki MR18.
endef

$(eval $(call KernelPackage,leds-nu801))

define KernelPackage/leds-reset
  SUBMENU:=$(LEDS_MENU)
  TITLE:=reset controller LED support
  DEPENDS:= @TARGET_ath79
  KCONFIG:=CONFIG_LEDS_RESET=m
  FILES:=$(LINUX_DIR)/drivers/leds/leds-reset.ko
  AUTOLOAD:=$(call AutoLoad,60,leds-reset,1)
endef

define KernelPackage/leds-reset/description
 Kernel module for LEDs on reset lines
endef

$(eval $(call KernelPackage,leds-reset))
