include $(TOPDIR)/rules.mk

PKG_NAME:=wg-nat-punch
PKG_VERSION:=0.01
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(BUILD_DIR)/wg-nat-punch

include $(INCLUDE_DIR)/package.mk

define Package/wg-nat-punch
  SECTION:=utils
  CATEGORY:=Utilities
#  DEPENDS:=
  TITLE:=wg-nat-punch
endef

define Package/wg-nat-punch/description
  Utilities to establish Wireguard connection in NAT-to-NAT scenario
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Package/wg-nat-punch/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/nat-punch-client $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/nat-punch-server $(1)/usr/bin/
endef

$(eval $(call BuildPackage,wg-nat-punch))
