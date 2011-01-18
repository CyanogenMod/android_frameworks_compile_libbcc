LLVM_AS := $(HOST_OUT_EXECUTABLES)/llvm-as$(HOST_EXECUTABLE_SUFFIX)
LLVM_RS_LINK  := $(HOST_OUT_EXECUTABLES)/llvm-rs-link$(HOST_EXECUTABLE_SUFFIX)

stub_module   := $(LOCAL_PATH)/$(LOCAL_MODULE:%.bc=%.ll)
linked_module := $(LOCAL_MODULE)
linked_module_fullpath := $(LOCAL_PATH)/$(LOCAL_MODULE)

llvm_ll_sources := $(filter %.ll,$(LOCAL_SRC_FILES))
llvm_ll_sources_fullpath := $(addprefix $(LOCAL_PATH)/, $(llvm_ll_sources))
PRIVATE_LL_SOURCE_FILES := $(llvm_ll_sources_fullpath)

llvm_bc_sources := $(llvm_ll_sources:.ll=.bc)
llvm_bc_sources_fullpath :=  $(addprefix $(LOCAL_PATH)/,$(llvm_bc_sources))
PRIVATE_BC_SOURCE_FILES := $(llvm_bc_sources_fullpath)

%.bc: %.ll
       $(call transform-ll-to-bc)

$(linked_module): $(PRIVATE_BC_SOURCE_FILES)
       $(call transform-host-bc-to-libruntime)

###########################################################
## Commands to compile LLVM Assembly
###########################################################
define transform-ll-to-bc
        $(hide) $(LLVM_AS) $< -o $@
endef


###########################################################
## Commands for running llvm-rs-link
###########################################################
define transform-host-bc-to-libruntime
touch $(stub_module)
$(hide) $(LLVM_AS) $(stub_module) -o $(linked_module_fullpath)

$(hide) $(LLVM_RS_LINK) -nostdlib \
         $(foreach bc,$(PRIVATE_BC_SOURCE_FILES),$(addprefix -l , $(bc))) \
         $(linked_module_fullpath)
endef
