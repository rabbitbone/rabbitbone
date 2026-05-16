# VM networking

Rabbitbone VM networking targets VMware NAT/bridged virtual Ethernet through Intel-compatible virtual NICs.

Supported VMware virtual NICs now:

- `e1000`: default for the bundled VMware examples, PCI `8086:100f` or `8086:100e`
- `e1000e`: supported when the VM has a free PCIe root-port slot, PCI `8086:10d3`

Not supported by this driver yet:

- `vmxnet3`, PCI `15ad:07b0`
- `vmxnet`, PCI `15ad:0720`
- `vlance`

Recommended VMware settings:

```vmx
ethernet0.present = "TRUE"
ethernet0.connectionType = "nat"
ethernet0.virtualDev = "e1000"
ethernet0.addressType = "generated"
ethernet0.startConnected = "TRUE"
```

If `net` prints `net: count=0`, run:

```sh
pci
```

The VM must show an Intel network controller such as `8086:10d3`, `8086:100f`, or `8086:100e`. If it shows `15ad:07b0`, the VM is using `vmxnet3`; change the VMX entry to `e1000`. If VMware reports that no PCIe slot is available, keep `e1000`; `e1000e` is PCIe and needs a free PCIe root-port slot.

Useful commands after boot:

```sh
net
netctl status
dhcp
ping 8.8.8.8
netctl dns example.com
ip 10.0.2.15/24 gw 10.0.2.2 dns 10.0.2.3
arp 10.0.2.2
```

`netctl` stores the active IPv4 lease in `/tmp/net0.conf` and writes `/etc/resolv.conf` when a DNS server is known.

## VMware Workstation PCIe slot error

If VMware Workstation shows `No PCIe slot available for Ethernet0`, use `ethernet0.virtualDev = "e1000"`. The bundled UEFI and BIOS examples use `e1000` so no PCIe root-port slot is required.
