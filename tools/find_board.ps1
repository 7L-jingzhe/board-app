# Find the board's current DHCP address by its stable Ethernet MAC.
#
# The board gets a new DHCP lease after some reboots. This script does a
# short ICMP sweep of the local /24, then reads the Windows neighbor table.
# It does not change the board's network configuration.
#
# Usage:
#   pwsh -File tools/find_board.ps1
#   pwsh -File tools/find_board.ps1 -Subnet 192.168.64 -Mac 46-76-B5-3E-F0-FD
param(
    [string]$Subnet = "192.168.64",
    [string]$Mac = "46-76-B5-3E-F0-FD"
)

$ErrorActionPreference = "Stop"
$normalizedMac = $Mac.ToUpper().Replace(":", "-")
$targets = 1..254 | ForEach-Object { "$Subnet.$_" }

Write-Host "Scanning $Subnet.0/24 for $normalizedMac ..."
# Parallel ICMP sweep; the board answers ping on its DHCP address.
$targets | ForEach-Object -Parallel {
    $null = Test-Connection -ComputerName $_ -Count 1 -TimeoutSeconds 1 -Quiet
} -ThrottleLimit 64

$neighbors = @(Get-NetNeighbor -AddressFamily IPv4 -ErrorAction SilentlyContinue |
    Where-Object {
        $_.LinkLayerAddress -and
        $_.LinkLayerAddress.ToUpper().Replace(":", "-") -eq $normalizedMac -and
        $_.IPAddress -like "$Subnet.*"
    })

if ($neighbors.Count -eq 0) {
    # Some Windows builds do not keep the entry in Get-NetNeighbor; fall back
    # to the legacy ARP table.
    $arp = arp -a
    $pattern = ($normalizedMac -replace "-", "-")
    $neighbors = @($arp | Select-String -Pattern $pattern | ForEach-Object {
        if ($_.Line -match "(\d+\.\d+\.\d+\.\d+)\s+([0-9a-fA-F-]{17})") {
            [pscustomobject]@{ IPAddress = $matches[1]; LinkLayerAddress = $matches[2] }
        }
    })
}

if ($neighbors.Count -eq 0) {
    Write-Error "Board MAC $normalizedMac not found. Is the board powered and on $Subnet.0/24?"
}

$neighbors |
    Select-Object -ExpandProperty IPAddress -Unique |
    ForEach-Object { Write-Output "BOARD_IP=$_" }
