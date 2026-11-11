import subprocess, os
sdk = "C:/Users/Michael/Desktop/Open Code Projects/IxView/.deps/sdk/ucrt64/bin"
env = dict(os.environ)
env["PATH"] = sdk + ";" + env["PATH"]
log = open("C:/Users/Michael/Desktop/Open Code Projects/IxView/build/ixui-build5.log", "w")
p = subprocess.Popen([sdk + "/ninja.exe", "-C",
                      "C:/Users/Michael/Desktop/Open Code Projects/IxView/build",
                      "-j4", "ixui"],
                     stdout=log, stderr=subprocess.STDOUT, env=env)
print("started pid", p.pid)
