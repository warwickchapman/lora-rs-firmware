Import("env")

# For OTA envs, VS Code "Upload and Monitor" can inject a serial --upload-port
# and break espota. Force the env-configured OTA IP/hostname instead.
if env.subst("$UPLOAD_PROTOCOL") == "espota":
    ota_ip = env.GetProjectOption("custom_ota_ip", default="")
    if ota_ip:
        env.Replace(UPLOAD_PORT=ota_ip)
    ota_auth = env.GetProjectOption("custom_ota_auth", default="")
    if ota_auth:
        env.Append(UPLOADERFLAGS=["--auth=%s" % ota_auth])
