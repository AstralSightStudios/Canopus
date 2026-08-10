# Band-9 interconnect/miwear layer — confirmed function mapping

Firmware: vela_ap.bin sha256 4f43b325addd6d9e6e7c7e2a4d00ffe3f23d5fb1560d8fe503544002ac1f516b
Version: 3.1.175  Build: CONBINE_LTALM054_T1175_04141021_release_5793
XIP: file offset 0 -> virtual 0x2C080000 (canonical 0x0C080000); +0x20000000 for vaddr

| band-10 (0x0C...) | band-9 (0x0C...) | kind | notes |
|---|---|---|---|
| interconnect_connect 0x0C2D2034 | sub_C1134D4 | fn | 5-arg (loop, conn, name, "miwear-server", cb); band-10 is 4-arg |
| interconnect_send 0x0C2D20C4 | sub_C114044 | fn | 5-arg (handle, name, msg, done, arg); matches band-10 |
| interconnect_close 0x0C2D2198 | sub_C4F7170 | fn | 1-arg; checks conn[8], queues teardown |
| quickapp_register_app 0x0C527E38 | sub_C2C655C | fn | 2-arg (app_id u16, app_info); copies 9 words |
| uv_miwear_message_recv_cb 0x0CAA35A0 | sub_C43BA64 | fn | routes apps/algo/capture, s2c/c2s_queue |
| __miwear_connect_cb 0x0C8876A0 | sub_C3D08D0 | fn | AIOTJS recv; msg types 2/3/5/6/7 + 131 |
| __miwear_send 0x0C887C7C | sub_C3CE284 | fn | AIOTJS send |
| __miwear_connect 0x0C888060 | sub_C3CE15C | fn | AIOTJS connect entry |
| property_get(interconnect.appname) 0x0C66B8C0 | sub_C35F50C | fn | reads "interconnect.appname" |
| interconnect_loop global 0x20121F90 | dword_200C9D28 | global | quickapp proxy loop |
| server create 0x0C2D1EF0 | sub_C11733C | fn | named-server factory |
| server object 0x2010E50C | dword_200CCFE0 | global | miwear-server object |

KEY ABI DIFFERENCE: band-9 interconnect_connect is 5-arg (passes server name string),
band-10 is 4-arg (server name baked into the framework). Native bindings must adapt.

| app_install 0x0CA519AC | sub_C44B5D0 | fn | int(descriptor,pages[],count); off_200A28D0 |
| clock_gettime 0x0C1EC8B4 | sub_C0BBB10 | fn | int(uint32_t, stock_timespec_t*) 12-byte |
| property_get(interconnect.appname) | sub_C35F50C | fn | |
| app registry table 0x200EB058 | off_200A28C8 | global | |
| app_install fn ptr 0x200EB060 | off_200A28D0 | global | |

## NuttX VFS layer

| band-10 (0x0C...) | band-9 (0x0C...) | kind | notes |
|---|---|---|---|
| open 0x0C1C15B0 | sub_C37F760 | fn | int(const char*,int,...); nx_open=sub_C37DC08, fs_getfilep=sub_C37D3FC, fd_allocfd=sub_C4F170C; sets errno |
| close 0x0C1AAB70 | sub_C37EFF8 | fn | int(int); wraps nx_close=sub_C37AB5C -> sub_C4F16A0(filelist,fd); fs_close=sub_C37C650; sets errno |
| read 0x0C1C1E24 | sub_C37F9EA | fn | int32(int,void*,uint32_t); fs_read=sub_C37CE70; sets errno |
| write 0x0C1C31C8 | sub_C380106 | fn | int32(int,const void*,uint32_t); fs_write=sub_C37AC34 (ops->write @+12); NULL buf->EINVAL; sets errno |
| register_driver 0x0C1A0D50 | sub_C4F0108 | fn | int(const char*,fops,priv) — band-9 DROPS mode arg (3-arg); inode_reserve=sub_C4F0044; register_blockdriver=sub_C4DB968 |
| unregister_driver 0x0C1A0E1C | sub_C381C00 | fn | int(const char*); inode_lock->inode_remove(sub_C37C518)->inode_unlock |

## Bluetooth (btservice/btm_gap) layer — band-9 uses Bluelet stack, names differ from band-10

| band-10 (0x0C...) | band-9 (0x0C...) | kind | notes |
|---|---|---|---|
| bt_adapter_get_instance 0x0CA286C8 | sub_C3BCAA8 | fn | void*(); lazy service singleton &unk_200ADDEC, vtable dword_200D6340=&unk_200ADE14 |
| bt_discovery_start 0x0C398D60 | sub_C3BFFC0 | fn | btm_start_discovery int(handle,mode); gap_iface+68 |
| bt_discovery_stop 0x0C398D8C | sub_C3C002C | fn | btm_stop_discovery int(handle); gap_iface+72 |
| bt_pair_request_reply 0x0C39988C | sub_C3BFD48 | fn | btm_reply_pair_request int(handle); gap_iface+36 |
| bt_pair_display_reply 0x0C3998C8 | sub_C3BFD94 | fn | bm_ssp_reply int(handle); gap_iface+40 (SSP display reply) |
| bt_create_bond 0x0C3A01A8 | sub_C3BFDE0 | fn | btm_create_bond int(handle,addr6); gap_iface+44; bts_create_bond=sub_C3BDD70 |
| bt_remove_bond 0x0C3A028C | sub_C3BFE98 | fn | btm_remove_bond int(handle,addr6); gap_iface+52; bts_remove_bond=sub_C3BDE84 |
| bt_adapter_register 0x0C398C24 | — | not found | band-9 uses bt_mgr_init sub_C3C2DCC (service->init registers callbacks) — different object model |
| bt_adapter_get_state 0x0C398D30 | — | not found | no adapter-state getter in band-9 btm_gap |
| bt_get_bond_state 0x0C39F370 | — | not found | only bond-state callback (btm_bond_state_changed_callback=sub_C3BF9FC), no getter |
| bt_get_pairing_state 0x0C39F9B0 | — | not found | no pairing-state getter found |
| bt_buffer_new 0x0C7D294C | — | not found | no Zephyr-style bt library in band-9 |
| bt_l2cap_connect 0x0C7ED49C | — | not found | band-9 BT is Bluelet stack (btm/bts/gap_if), no bt_l2cap API |
| bt_l2cap_disconnect 0x0C7ED54C | — | not found | same |
| bt_l2cap_submit_cid 0x0C7ED578 | — | not found | same |
| bt_alloc 0x0C828454 | — | not found | same |
| bt_free 0x0C828460 | — | not found | same |
| bt_timer_add 0x0C7D2C00 | — | not found | same |
| bt_timer_cancel 0x0C7D2CCC | — | not found | same |
| bt_queue_external 0x0C7D335C | — | not found | same |
| bt_l2cap_owner 0x20137B1C | — | global not found | no l2cap owner global |
| sdp_builder_create 0x0C7F2014 | — | not found | no sdp_* API in band-9 |
| sdp_set_raw_attribute 0x0C7EFBD4 | — | not found | same |
| sdp_commit 0x0C7EFFD8 | — | not found | same |
| sdp_unregister 0x0C7F20C4 | — | not found | same |
