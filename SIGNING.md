# Signing a custom kernel for Secure Boot

(Instructions are for ubuntu, but should work similar for other distros, if they are using shim
and grub as bootloader.)

Since the most recent GRUB2 update (2.02+dfsg1-5ubuntu1) in Ubuntu, GRUB2 does not load unsigned
kernels anymore, as long as Secure Boot is enabled. Users of Ubuntu 18.04 will be notified during
upgrade of the grub-efi package, that this kernel is not signed and the upgrade will abort.

Thus you have three options to solve this problem:

1. You sign the kernel yourself.
2. You use a signed, generic kernel of your distro.
3. You disable Secure Boot.

Since option two and three are not really viable, these are the steps to sign the kernel yourself:

Instructions adapted from [the Ubuntu Blog](https://blog.ubuntu.com/2017/08/11/how-to-sign-things-for-secure-boot).

1. Create the config to create the signing key, save as mokconfig.cnf:
```
# This definition stops the following lines failing if HOME isn't
# defined.
HOME                    = .
RANDFILE                = $ENV::HOME/.rnd 
[ req ]
distinguished_name      = req_distinguished_name
x509_extensions         = v3
string_mask             = utf8only
prompt                  = no

[ req_distinguished_name ]
countryName             = <YOURcountrycode>
stateOrProvinceName     = <YOURstate>
localityName            = <YOURcity>
0.organizationName      = <YOURorganization>
commonName              = Secure Boot Signing Key
emailAddress            = <YOURemail>

[ v3 ]
subjectKeyIdentifier    = hash
authorityKeyIdentifier  = keyid:always,issuer
basicConstraints        = critical,CA:FALSE
extendedKeyUsage        = codeSigning,1.3.6.1.4.1.311.10.3.6
nsComment               = "OpenSSL Generated Certificate"
```
Adjust all parts with <YOUR*> to your details.

2. Create the public and private key for signing the kernel:
```
openssl req -config ./mokconfig.cnf \
        -new -x509 -newkey rsa:2048 \
        -nodes -days 36500 -outform DER \
        -keyout "MOK.priv" \
        -out "MOK.der"
```

3. Convert the key also to PEM format (mokutil needs DER, sbsign needs PEM):
```
openssl x509 -in MOK.der -inform DER -outform PEM -out MOK.pem
```

4. Enroll the key to your shim installation:
```
sudo mokutil --import MOK.der
```
You will be asked for a password, you will just use it to confirm your key selection in the
next step, so choose any.

5. Restart your system. You will encounter a blue screen of a tool called MOKManager.
Select "Enroll MOK" and then "View key". Make sure it is your key you created in step 2.
Afterwards continue the process and you must enter the password which you provided in
step 4. Continue with booting your system.

6. Verify your key is enrolled via:
```
sudo mokutil --list-enrolled
```

7. Sign your installed kernel (it should be at /boot/vmlinuz-[KERNEL-VERSION]-surface-linux-surface):
```
sudo sbsign --key MOK.priv --cert MOK.pem /boot/vmlinuz-[KERNEL-VERSION]-surface-linux-surface --output /boot/vmlinuz-[KERNEL-VERSION]-surface-linux-surface.signed
```

8. Copy the initram of the unsigned kernel, so we also have an initram for the signed one.
```
sudo cp /boot/initrd.img-[KERNEL-VERSION]-surface-linux-surface{,.signed}
```

9. Update your grub-config
```
sudo update-grub
```

10. Reboot your system and select the signed kernel. If booting works, you can remove the unsigned kernel:
```
sudo mv /boot/vmlinuz-[KERNEL-VERSION]-surface-linux-surface{.signed,}
sudo mv /boot/initrd.img-[KERNEL-VERSION]-surface-linux-surface{.signed,}
sudo update-grub
```

Now your system should run under a signed kernel and upgrading GRUB2 works again. If you want
to upgrade the custom kernel, you can sign the new version easily by following above steps
again from step seven on. Thus BACKUP the MOK-keys (MOK.der, MOK.pem, MOK.priv).
