# 2026-10-29T12:49:30.179099
import vitis

client = vitis.create_client()
client.set_workspace(path="/scratch/643_vitis_suchirv/lab3")

status = client.set_preference(level = "WORKSPACE", device = "ZynqMP", key = "Sysroot", value = "/afs/ece.cmu.edu/class/ece643/software/xilinxVitis/platforms/2023.2/u96v2_sbc_base_2023_2/sw/sysroots/cortexa72-cortexa53-xilinx-linux")

comp = client.create_app_component(name="cnn_host",platform = "/afs/ece.cmu.edu/class/ece643/software/xilinxVitis/platforms/2023.2/u96v2_sbc_base_2023_2/export/u96v2_sbc_base_2023_2-vitis-platform/u96v2_sbc_base_2023_2-vitis-platform.xpfm",domain = "linux_psu_cortexa53")

comp = client.get_component("cnn_host")

status = comp.set_sysroot(sysroot="/afs/ece.cmu.edu/class/ece643/software/xilinxVitis/platforms/2023.2/u96v2_sbc_base_2023_2/sw/sysroots/cortexa72-cortexa53-xilinx-linux")

comp = client.get_component(name="cnn_host")
status = comp.import_files(from_loc="/scratch/643_vitis_suchirv/lab3/18643_cnn_src", files=["cnn.cpp"])

status = comp.import_files(from_loc="/scratch/643_vitis_suchirv/lab3/18643_cnn_src", files=["util643.h"])

status = comp.import_files(from_loc="/scratch/643_vitis_suchirv/lab3/18643_cnn_src", files=["kernel643.h"])

status = comp.import_files(from_loc="/scratch/643_vitis_suchirv/lab3/18643_cnn_src", files=["instance643.h"])

comp = client.create_hls_component(name="krnl_cnn", cfg_file = ["hls_config.cfg"], template = "empty_hls_component")

status = client.set_preference(level = "WORKSPACE", device = "ZynqMP", key = "KernelImage", value = "/afs/ece.cmu.edu/class/ece643/software/xilinxVitis/platforms/2023.2/u96v2_sbc_base_2023_2/sw/Image")

status = client.set_preference(level = "WORKSPACE", device = "ZynqMP", key = "RootFS", value = "/afs/ece.cmu.edu/class/ece643/software/xilinxVitis/platforms/2023.2/u96v2_sbc_base_2023_2/sw/rootfs.ext4")

proj = client.create_sys_project(name="cnn", platform="/afs/ece.cmu.edu/class/ece643/software/xilinxVitis/platforms/2023.2/u96v2_sbc_base_2023_2/export/u96v2_sbc_base_2023_2-vitis-platform/u96v2_sbc_base_2023_2-vitis-platform.xpfm", template="empty_accelerated_application")

proj = client.get_sys_project(name="cnn")

status = proj.add_container(name="cnn")

proj = proj.add_component(name="krnl_cnn", containers=["cnn"])

proj.build(target="hw")

proj = proj.add_component(name="cnn_host")

status = proj.clean(target="hw")

proj.build(target="hw")

status = comp.import_files(from_loc="/scratch/643_vitis_suchirv/lab3/18643_cnn_src", files=["cnn.h"])

status = proj.clean(target="hw")

proj.build(target="sw_emu")

proj.build(target="hw")

status = proj.clean(target="hw")

status = proj.clean(target="hw")

proj.build(target="hw")

status = proj.clean(target="hw")

proj.build(target="hw")

status = proj.clean(target="hw")

proj.build(target="hw")

status = proj.clean(target="hw")

proj.build(target="hw")

status = proj.clean(target="hw")

proj.build(target="hw")

status = proj.clean(target="hw")

proj.build(target="hw")

status = proj.clean(target="hw")

status = comp.clean(target="x86sim")

status = comp.clean(target="hw")

status = proj.clean(target="sw_emu")

