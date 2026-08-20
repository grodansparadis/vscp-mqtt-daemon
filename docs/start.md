![Very Simple Control Protocol](./images/logo_100.png "Very Simple Control Protocol") 

# The VSCP MQTT Daemon

**Describe software version**:${/var/document-version}      
**Doc update:** ${/var/creation-time}       
[history](./history.md)

Author Åke Hedman and [the VSCP project](https://www.vscp.org), [info@vscp.org](info@vscp.org)  

# Abstract

The VSCP MQTT daemon is an open source server program (daemon) that is part of [VSCP & Friends](https://www.vscp.org) and is built and maintained by [the VSCP project][vscplink]. The server act as a hub and collects information from different sources and let clients collect this information over MQTT. 

the [VSCP tcp/ip daemon][vscpTcpIpDaemonLink] is simular to it's functionality in many cases but export the VSCP link protocol interface to clients. See its [repository][vscpTcpIpDaemonLink] for more information.

![](images/vscp_arcitecture.png)

[filename](./bottom_copyright.md ':include')


[vscpTcpIpDaemonLink]: https://github.com/grodansparadis/vscp-tcpip-daemon
[vscpLink]: https://www.vscp.org
