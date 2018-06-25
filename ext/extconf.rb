require 'mkmf'
extension_name = 'AFS'
dir_config('afs')
$LIBPATH.push('/usr/lib/x86_64-linux-gnu/heimdal')
if (have_header('afs/ptuser.h') and
    have_library('resolv', 'res_search', 'resolv.h') and
    have_library('pthread', 'pthread_create', 'pthread.h') and
    have_library('roken', 'rk_socket') and
    have_library('hcrypto', 'hc_DES_cbc_encrypt') and
    have_library('afsrpc_pic', 'rx_SetNoJumbo', 'rx/rx.h') and
    have_library('afsauthent_pic', 'pr_Initialize', 'afs/ptuser.h'))
  have_func('afs_error_message', ['afs/stds.h', 'afs/com_err.h'])
  create_makefile(extension_name)
end
