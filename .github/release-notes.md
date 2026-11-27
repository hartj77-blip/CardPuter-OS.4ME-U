## Before you update: SSH and the default password

From 0.8.0, SSH takes only SSH keys while the password is still the default `raspberry`. Everyone knows that password, so anyone on the same Wi-Fi could otherwise log in before you do.

- Change the password first: open the **Terminal** app (it asks for a new one), or run `passwd`. Then `ssh pi@cardputer.local` works with the new password.
- If you added an SSH key or your own password in Raspberry Pi Imager, SSH works straight away.
- If you only ever log in over SSH with `raspberry`, change the password on the device before or right after updating, or you will be locked out of SSH until you do.
