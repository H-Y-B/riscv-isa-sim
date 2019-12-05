require_extension('S');
bool vm_enabled = get_field(STATE.satp, (xlen == 32 ? SATP32_MODE : SATP64_MODE)) != SATP_MODE_OFF;
require(vm_enabled);
require_privilege(get_field(STATE.mstatus, MSTATUS_TVM) ? PRV_M : PRV_S);
MMU.flush_tlb();
