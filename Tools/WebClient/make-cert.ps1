# wow335modding: HTTPS certificates for serving the web client on your local network (serve.py --cert/--key).
# Browsers only run the client (it needs threads, so a cross-origin isolated page) over https or on localhost.
#
# Makes a small certificate authority of your own (certs\wow-ca.crt) and a server certificate signed by it, valid for
# this PC's name, localhost and every IPv4 address given (default: this PC's LAN addresses). Install wow-ca.crt once
# on each device that should play, as a trusted root, and the site opens without warnings:
#   Windows: double-click wow-ca.crt > Install Certificate > Current User > Place all certificates in the following
#            store > Trusted Root Certification Authorities. Chrome and Edge use the Windows store.
# Running the script again reuses the CA, so devices keep trusting new server certificates (e.g. after the IP changes).
#
#   powershell -ExecutionPolicy Bypass -File Tools\WebClient\make-cert.ps1 [-Addresses 192.168.2.9]
param([string[]]$Addresses)

$openssl = "C:\Program Files\OpenSSL-Win64\bin\openssl.exe"
if (-not (Test-Path $openssl)) { $openssl = "openssl" }

if (-not $Addresses) {
    $Addresses = Get-NetIPAddress -AddressFamily IPv4 |
        Where-Object { $_.PrefixOrigin -eq "Dhcp" -or ($_.PrefixOrigin -eq "Manual" -and $_.InterfaceAlias -notlike "vEthernet*") } |
        ForEach-Object { $_.IPAddress }
}

$dir = Join-Path $PSScriptRoot "certs"
New-Item -ItemType Directory -Force $dir | Out-Null
$caKey = Join-Path $dir "wow-ca.key"
$caCert = Join-Path $dir "wow-ca.crt"

# The CA: only ever signs this server's certificates. Its key stays on this PC.
if (-not (Test-Path $caKey)) {
    & $openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -subj "/CN=WoW web client CA ($env:COMPUTERNAME)" `
        -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" `
        -keyout $caKey -out $caCert
}

$names = @("DNS:localhost", "DNS:$env:COMPUTERNAME", "IP:127.0.0.1") + ($Addresses | ForEach-Object { "IP:$_" })
$extensions = Join-Path $dir "server.ext"
Set-Content -Encoding ascii $extensions @(
    "basicConstraints=CA:FALSE",
    "keyUsage=critical,digitalSignature,keyEncipherment",
    "extendedKeyUsage=serverAuth",
    "subjectAltName=$($names -join ',')"
)

& $openssl req -newkey rsa:2048 -nodes -subj "/CN=WoW web client ($env:COMPUTERNAME)" `
    -keyout (Join-Path $dir "wow.key") -out (Join-Path $dir "wow.csr")
& $openssl x509 -req -in (Join-Path $dir "wow.csr") -CA $caCert -CAkey $caKey -CAcreateserial -days 825 `
    -extfile $extensions -out (Join-Path $dir "wow.pem")
Remove-Item (Join-Path $dir "wow.csr"), $extensions

Write-Host ""
Write-Host "Server certificate for $($names -join ', ')"
Write-Host "Install $caCert as a trusted root on each device that should play (see the top of this script)."
Write-Host "Run: python Tools\WebClient\serve.py --host 0.0.0.0 --cert Tools\WebClient\certs\wow.pem --key Tools\WebClient\certs\wow.key"
