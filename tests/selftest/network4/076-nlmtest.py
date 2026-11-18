# the Network List Manager (userland/netprofm): nlmtest asks netprofm's
# NetworkListManager about QEMU's network (the one the firewall identified
# by its gateway), changes its category and name with an INetworkEvents
# sink advised, and puts them back
DOC = '`nlmtest` (netprofm INetworkListManager: connected to the Internet over IPv4, the network the PC is on with the name, category and id under NetworkList\\Profiles, every known network, GetNetwork by id; the connection on iphlpapi\'s adapter GUID; INetworkCostManager and INetworkConnectionCost unrestricted; SetCategory and SetName reported to an advised INetworkEvents sink, Domain refused, Unadvise), 64- and 32-bit'

TESTS = [
    Test('nlmtest', 'nlmtest', [r'nlmtest: \d+ passed, 0 failed'], elevated=True, timeout=120),
    Test('nlmtest x86', r'C:\Programs\x86\nlmtest.exe', [r'nlmtest: \d+ passed, 0 failed'], elevated=True,
         timeout=120),
]
