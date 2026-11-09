# Dev setup: SSH from carbon to the Windows PC

This lets Claude on carbon copy builds to DESKTOP-CARLOS and run them there
(DEVPLAN tasks 0.3, 1.3, 1.8 and others), without asking the user to relay
messages between the machines.

- Key on carbon: `~/.ssh/id_ed25519_desktop`. It's a dedicated key, used only
  for this.
- Public key:
  ```
  ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIANZ0vhnDJkBQYvja5Q0AGqLcjdlF7DJPT2dO6rMXqZv carbon->desktop-carlos (duckling)
  ```

## On Windows: PowerShell **as Administrator**

```powershell
# 1. Install and start the OpenSSH server
Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
Start-Service sshd
Set-Service sshd -StartupType Automatic

# 2. Firewall: SSH only from the local network, and only when the network is "Private"
#    (carbon's IP is dynamic, so allow the local subnet instead of one address)
Get-NetFirewallRule -Name OpenSSH-Server-In-TCP | Set-NetFirewallRule -Profile Private -RemoteAddress LocalSubnet

# 3. Authorize carbon's key. The account is an administrator, so it goes in the admin file
$key = 'ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIANZ0vhnDJkBQYvja5Q0AGqLcjdlF7DJPT2dO6rMXqZv carbon->desktop-carlos (duckling)'
Add-Content -Path C:\ProgramData\ssh\administrators_authorized_keys -Value $key
icacls C:\ProgramData\ssh\administrators_authorized_keys /inheritance:r /grant "Administrators:F" /grant "SYSTEM:F"

# 4. Key only: turn off password logins
(Get-Content C:\ProgramData\ssh\sshd_config) -replace '^#?PasswordAuthentication .*','PasswordAuthentication no' | Set-Content C:\ProgramData\ssh\sshd_config

# 5. Use PowerShell as the SSH shell instead of cmd
New-ItemProperty -Path HKLM:\SOFTWARE\OpenSSH -Name DefaultShell -Value "C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe" -PropertyType String -Force

Restart-Service sshd

# 6. Tell Claude the user name
whoami
```

Notes:
- **The `icacls` line is required.** Windows ignores the key file unless only
  Administrators and SYSTEM can access it.
- **Network type:** the network must be set to Private in Windows (Settings →
  Network → properties). Otherwise the firewall rule doesn't apply.
- **Optional, stricter:** in the FRITZ!Box (`http://fritz.box`, Home Network
  → Network → carbon), enable "Always assign the same IPv4 address". Then
  replace `LocalSubnet` with that IP.

## What this allows, and how to undo it
Anything on carbon that can read `~/.ssh/id_ed25519_desktop` can run commands
on the Windows PC as an administrator, from the local network only.

To revoke it, use either of these:
- Delete the line from `C:\ProgramData\ssh\administrators_authorized_keys`.
- Run `Stop-Service sshd; Set-Service sshd -StartupType Disabled`.
