# only build this if FORCE_BUILD is set or MY_TARGET is in the ALL list
ifeq ($(if $(FORCE_BUILD),1,$(call FINDINLIST,$(MY_TARGET),$(ALL))),1)

MY_TARGET_IN := $(MY_TARGET)
MY_TARGETDIR_IN := $(MY_TARGETDIR)
MY_SRCDIR_IN := $(MY_SRCDIR)
MY_SRCS_IN := $(MY_SRCS)
MY_EXTRAOBJS_IN := $(MY_EXTRAOBJS)
MY_COMPILEFLAGS_IN := $(MY_COMPILEFLAGS)
MY_CFLAGS_IN := $(MY_CFLAGS)
MY_CPPFLAGS_IN := $(MY_CPPFLAGS)
MY_LDFLAGS_IN := $(MY_LDFLAGS)
MY_INCLUDES_IN := $(MY_INCLUDES)
MY_LIBS_IN := $(MY_LIBS)
MY_LIBPATHS_IN := $(MY_LIBPATHS)
MY_DEPS_IN := $(MY_DEPS)
MY_LINKSCRIPT_IN := $(MY_LINKSCRIPT)

# if the targetdir isn't passed in, construct it from the srcdir path
ifeq ($(MY_TARGETDIR_IN),)
MY_TARGETDIR_IN := $(call TOBUILDDIR,$(MY_SRCDIR_IN))
endif

#$(warning MY_SRCDIR_IN = $(MY_SRCDIR_IN))
#$(warning MY_TARGETDIR_IN = $(MY_TARGETDIR_IN))

# extract the different source types out of the list
#$(warning MY_SRCS_IN = $(MY_SRCS_IN))
MY_CPPSRCS_IN := $(filter %.cpp,$(MY_SRCS_IN))
MY_CSRCS_IN := $(filter %.c,$(MY_SRCS_IN))
MY_ASMSRCS_IN := $(filter %.S,$(MY_SRCS_IN))

#$(warning MY_CPPSRCS_IN = $(MY_CPPSRCS_IN))
#$(warning MY_CSRCS_IN = $(MY_CSRCS_IN))
#$(warning MY_ASMSRCS_IN = $(MY_ASMSRCS_IN))

# build a list of objects
MY_CPPOBJS_IN := $(addprefix $(MY_TARGETDIR_IN)/,$(patsubst %.cpp,%.o,$(MY_CPPSRCS_IN)))
MY_COBJS_IN := $(addprefix $(MY_TARGETDIR_IN)/,$(patsubst %.c,%.o,$(MY_CSRCS_IN)))
MY_ASMOBJS_IN := $(addprefix $(MY_TARGETDIR_IN)/,$(patsubst %.S,%.o,$(MY_ASMSRCS_IN)))
_TEMP_OBJS := $(MY_ASMOBJS_IN) $(MY_CPPOBJS_IN) $(MY_COBJS_IN) $(MY_EXTRAOBJS_IN)
#$(warning _TEMP_OBJS = $(_TEMP_OBJS))

# add to the global object list
ALL_OBJS := $(ALL_OBJS) $(_TEMP_OBJS)

# add to the global deps
ALL_DEPS := $(ALL_DEPS) $(_TEMP_OBJS:.o=.d)

$(MY_TARGET_IN): MY_LDFLAGS_IN:=$(MY_LDFLAGS_IN)
$(MY_TARGET_IN): MY_LIBS_IN:=$(MY_LIBS_IN)
$(MY_TARGET_IN): MY_LIBPATHS_IN:=$(MY_LIBPATHS_IN)
$(MY_TARGET_IN): MY_LINKSCRIPT_IN:=$(MY_LINKSCRIPT_IN)
$(MY_TARGET_IN): _TEMP_OBJS:=$(_TEMP_OBJS)
$(MY_TARGET_IN):: $(_TEMP_OBJS) $(MY_DEPS_IN) $(MY_GLUE_IN)
	@$(MKDIR)
	@echo linking $@
	$(NOECHO)$(LD) $(GLOBAL_LDFLAGS) $(MY_LDFLAGS_IN) --script=$(MY_LINKSCRIPT_IN) -L $(LIBS_BUILD_DIR) $(MY_LIBPATHS_IN) -o $@ $(_TEMP_OBJS) $(MY_LIBS_IN) $(LIBGCC)
	@echo creating listing file $@.lst
	$(NOECHO)$(OBJDUMP) -C -S $@ > $@.lst

include templates/compile.mk

endif # find in ALL

MY_TARGET :=
MY_TARGETDIR :=
MY_SRCDIR :=
MY_SRCS :=
MY_EXTRAOBJS :=
MY_COMPILEFLAGS :=
MY_CFLAGS :=
MY_CPPFLAGS :=
MY_LDFLAGS :=
MY_INCLUDES :=
MY_LIBS :=
MY_LIBPATHS :=
MY_DEPS :=
MY_LINKSCRIPT :=
FORCE_BUILD :=