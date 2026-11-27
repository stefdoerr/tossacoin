######################################
#
# tossacoin
#
######################################

# Buildroot recipe for mod-plugin-builder and the MOD Cloud Builder
# (builder.mod.audio/buildroot). Upload this file as is: the builder takes the
# bundle name from the file name. `make release` bumps the tag below.

TOSSACOIN_VERSION = v0.1.0
TOSSACOIN_SITE = https://github.com/stefdoerr/tossacoin.git
TOSSACOIN_SITE_METHOD = git
TOSSACOIN_GIT_SUBMODULES = y
TOSSACOIN_BUNDLES = tossacoin.lv2

# needed for submodules support (dpf)
TOSSACOIN_PRE_DOWNLOAD_HOOKS += MOD_PLUGIN_BUILDER_DOWNLOAD_WITH_SUBMODULES

TOSSACOIN_TARGET_MAKE = $(TARGET_MAKE_ENV) $(TARGET_CONFIGURE_OPTS) $(MAKE) -C $(@D) PLUGIN_FORMATS=lv2

define TOSSACOIN_BUILD_CMDS
	$(TOSSACOIN_TARGET_MAKE)
endef

define TOSSACOIN_INSTALL_TARGET_CMDS
	install -d $(TARGET_DIR)/usr/lib/lv2
	cp -rL $(@D)/bin/tossacoin.lv2 $(TARGET_DIR)/usr/lib/lv2/
endef

$(eval $(generic-package))
