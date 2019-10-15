/* original Copyright (c) 2018 Pablo Marcos Oltra <pablo.marcos.oltra@gmail.com>
 * additions Copyright (c) 2019 Philippe Joyez
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
 // (hence this code is under the "MIT Licence")
/*
 * This exec.so library is intended to be used together with the AppImage distribution
 * mechanism. 
 * Its purpose is to restore a suitable environment for processes that are called 
 * outside of the AppImage. In particular it unsets the LD_LIBRARY_PATH that points
 * to libraries bundled with the AppImage that may clash with the distribution's version
 * expected by the external processes. 
 * 
 * Usage is as follows:
 *
 * 1. AppRun injects this library to the dynamic loader through LD_PRELOAD so that it
 *    masks the standard execve, system etc. UNIX functions.
 *    e.g `export LD_PRELOAD=$APPDIR/usr/optional/exec.so` 
 *    Apprun also sets LD_LIBRARY_PATH, typically to $APPDIR/usr/lib for the AppImage 
 *    executable to find its libraries.
 *
 * 2. This library will intercept calls to create new processes and will try to figure whether
 *    those calls are for binaries within the AppImage bundle or external ones.
 *    (note that is is not presently foolproof: if you call an script inside the appimage 
 *    marked as executable but with the interpreter outside of the appimage, it will not
 *    recognize it is an external process).  
 *
 * 3. In case it's an internal process, it will not change anything.
 *    In case it's an external process, the behavior depends whether the 
 *    environment variable APPIMAGE_PRESERVE_ENV_PREFIX is not set or not:
 * 
 * 3a. if APPIMAGE_PRESERVE_ENV_PREFIX is not set to a value
 *    remove the LD_PRELOAD , LD_LIBRARY_PATH and APPDIR
 *    entries from the environment of the new process. Any other environment 
 *    variables set by the AppImaged program are thus passed onto the child process.
 * 
 * 3b. if APPIMAGE_PRESERVE_ENV_PREFIX is set to a value 
 *    we search up the process tree the first process that is not in the appimage
 *    (APPDIR not set) and copy its _initial_ environment (from /proc/<pid>/environ)
 *    to a new environment. Then, we add to the new environment
 *    any entry of the current environment that starts with the defined prefix and
 *    launch the new process with the new environment. This assumes all the 
 *    env variable the program needs to pass to the child begin with that prefix.
 *    
 *    For TeXmacs, one could for instance set APPIMAGE_PRESERVE_ENV_PREFIX=TEXMACS_
 *    However in TeXmacs this last approach fails when launching a Shell plugin: it
 *    ends up with an empty environment and pipe communications with TeXmacs not
 *    working... (For some yet unknown reason there seems to be an error while working
 *    out the ancestors' environment.)
 *    The other method works.
 * 
 *  Note: If the application calls "system()" for internal appimage processes, you need to
 *    embbed a shell interpreter in the AppImage under $APPDIR/bin/sh. e.g.:
 *    cp /bin/bash $BUILD_APPDIR/bin ; ln -srT $BUILD_APPDIR/bin/bash $BUILD_APPDIR/bin/sh
 * 
 */

#define _GNU_SOURCE

#include "env.h"
#include "debug.h"

#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <string.h>
#include <errno.h>
#include <spawn.h>
#include <signal.h>
#include <sys/wait.h>
#include <linux/limits.h>


char* APPIMAGE_PRESERVE_ENV_PREFIX = "APPIMAGE_PRESERVE_ENV_PREFIX";


extern char **environ;


typedef int (*execve_func_t)(const char *filename, char *const argv[], char *const envp[]);
typedef int (*posix_spawn_func_t)(pid_t *pid, const char *filename, 
                       const posix_spawn_file_actions_t *file_actions,
                       const posix_spawnattr_t *attrp,
                       char *const argv[], char *const envp[]);

#define MIN(X,Y) ((X) < (Y) ? (X) : (Y))

const char* resolve_in_path(const char* filename){ //use shell to find in PATH, returning full path
  char cmd[100] = {0};

  //snprintf(cmd, sizeof(cmd), "PATH=%s /bin/which %s", getenv("PATH") , filename);
  //snprintf(cmd, sizeof(cmd), "/bin/which %s", filename);
  snprintf(cmd, sizeof(cmd), "command -v %s", filename); //works for 3a ,but tm_shell does not work with 3b 

  FILE *fp;
  char *path = calloc(PATH_MAX , sizeof(char));

  /* Open the command for reading. */
  fp = popen(cmd, "r");
  if (fp == NULL) {
    DEBUG("Failed to run which command\n" );
    return filename;
    }
  else {
    /* Read the output a line at a time - output it. */
    if (fgets(path, PATH_MAX, fp) != NULL) {
      DEBUG("which result %s", path);
      }
    /* close */
    pclose(fp);
    return (strlen(path)>0 ? path : filename);
    }
}

static int is_external_process(const char *filename) {
    const char *appdir = getenv("APPDIR");
    if (!appdir)
        return 0;
    DEBUG("APPDIR = %s\n", appdir);
    const char *fullpath = canonicalize_file_name(filename);
    DEBUG("command %s, canonical path %s\n", filename, fullpath);
    if (!fullpath) {
         fullpath = resolve_in_path (filename);
         DEBUG("fullpath from resolve_in_path %s\n", fullpath);
         }

    int ret = strncmp(fullpath, appdir, MIN(strlen(fullpath), strlen(appdir)));
    if (fullpath != filename)
      free((char*)fullpath);
    return !(ret == 0);  
}

char* const* external_environment(char* const envp[]) {
    DEBUG("External process detected. Restoring env vars.\n");
    //DEBUG("current process PID: %d\n", getpid());

    const char *RESTORE_ENV_PREFIX = getenv(APPIMAGE_PRESERVE_ENV_PREFIX);
    int envc = env_len(envp);
    int transfers = 0 ;
    int i;
    char** newenv = NULL;
    if (!RESTORE_ENV_PREFIX) {
        DEBUG("Environment prefix to pass to child not defined in %s\n", APPIMAGE_PRESERVE_ENV_PREFIX);
        //simply cleanup environment of LD_PRELOAD and LD_LIBRARY_PATH
        //(we assume they were not set before starting the appimage)
        newenv = env_allocate(envc); 
        for ( i = 0; i < envc; i++ ) {
            char* line = envp[i];
            if (( strncmp(line, "LD_PRELOAD", 10) ) \
                && ( strncmp(line, "LD_LIBRARY_PATH", 15))  \
                && ( strncmp(line, "APPDIR", 6)) \
                ) { 
              newenv[transfers]= envp[i];
              transfers++;
              }
            else {
              DEBUG("Removing from env : %s\n", line);
              }
            }
        DEBUG("Found %d env variables to pass\n", transfers);    
        newenv[transfers]=0;
        }
    else {
        int prefix_len = strlen(RESTORE_ENV_PREFIX);
        char** tmpenv = env_allocate(envc); 
        for ( i = 0; i < envc; i++ ) {
            char* line = envp[i];
            if ( !strncmp(line, RESTORE_ENV_PREFIX, prefix_len) ) { //an environment variable starts with the keyword, we should transfer it
              //DEBUG("Found prefixed env variables : %s\n", line);
              tmpenv[transfers]= envp[i];
              transfers++;
              }
            else if ( !strncmp(line, "PATH=", 5) ) {
              DEBUG("saving PATH : %s\n", line);
              tmpenv[transfers]= envp[i];
              transfers++;
              }
            }
        DEBUG("Found %d env variables to pass\n", transfers);    
        
        //now copy grandparent env and add to it the env variable we want to transfer
        char* const* gpenv = read_env_recursive();
        int gpenvc = env_len(gpenv);
        char** newenv = env_allocate(gpenvc + transfers); //larger new env
        // copy grandparent env in new env
        int j = 0;
        for ( i = 0; i < gpenvc ; i++ ) {
            if ( strncmp(gpenv[i], "PATH=", 5) != 0 ) {//do not copy PATH, we have it already
               newenv[j] = strdup(gpenv[i]);
               j++;
               }
            }
        DEBUG("newenv: %zu\n", env_len(newenv));   
            
        if (transfers > 0) {
            for ( i = 0; i < transfers; i++ ) {
                newenv[j+i]= strdup(tmpenv[i]);
                //DEBUG("copying : %s\n", tmpenv[i]);
                }
            }
        DEBUG("oldenv: %zu ; tmpenv: %zu ; gpenv: %d ; newenv: %zu\n", env_len(envp), env_len(tmpenv), gpenvc, env_len(newenv));   
        newenv[j+transfers]=0;
        env_free(gpenv);
        free(tmpenv);
    }
    return (char* const*) newenv;
}

int execve(const char *filename, char *const argv[], char *const envp[]) {
    DEBUG("execv(e) call hijacked: %s\n", filename);
    int external = is_external_process(filename);
    int ret;
    execve_func_t execve_orig = dlsym(RTLD_NEXT, "execve");
    if (external){ 
        DEBUG("passing to execve now (external process)\n");
        char* const *env = external_environment(envp);
        ret = execve_orig (filename, argv, env);
        env_free(env);
        }
    else {
        DEBUG("passing to execve now (internal appimage process)\n");
        ret = execve_orig (filename, argv, envp);
        }
    return ret;
}


int execv(const char *filename, char *const argv[]) {
    return execve (filename, argv, environ);
    }


int execvpe(const char *filename, char *const argv[], char *const envp[]) {
    DEBUG("execvp(e) call hijacked: %s\n", filename);
    int external = is_external_process(filename);
    int ret;
    execve_func_t execvpe_orig = dlsym(RTLD_NEXT, "execvpe");
    if (external){ 
        DEBUG("passing to execvpe now (external process)\n");
        char* const *env = external_environment(envp);
        ret = execvpe_orig (filename, argv, env);
        env_free(env);
        }
    else {
        DEBUG("passing to execvpe now (internal appimage process)\n");
        ret = execvpe_orig (filename, argv, envp);
        }
    return ret;
}

int execvp(const char *filename, char *const argv[]) {
    return execvpe (filename, argv, environ);
}


int posix_spawnp(pid_t *pid, const char *filename,
                       const posix_spawn_file_actions_t *file_actions,
                       const posix_spawnattr_t *attrp,
                       char *const argv[], char *const envp[]) {
    DEBUG("posix_spawnp call hijacked: %s\n", filename);
    posix_spawn_func_t posix_spawnp_orig = dlsym(RTLD_NEXT, "posix_spawnp");
    if (!posix_spawnp_orig) {
        DEBUG("Error getting posix_spawnp original symbol: %s\n", strerror(errno));
        return -1;
        }
    int external = is_external_process(filename);
    int ret;
    if (external){
        char* const *env = external_environment(envp);
        ret = posix_spawnp_orig(pid, filename, file_actions, attrp, argv, env);
        env_free(env);
        }
    else {
        ret = posix_spawnp_orig(pid, filename, file_actions, attrp, argv, envp);
        }
    return ret;
}


//re-implementation of system based on https://pubs.opengroup.org/onlinepubs/9699919799/functions/system.html
//(and adapted for our needs) 
int system(const char *cmd)
{
    DEBUG("system called for %s\n", cmd);
    int stat;
    pid_t pid;
    struct sigaction sa, savintr, savequit;
    sigset_t saveblock;
    if (cmd == NULL)
        return(1);
    char *cmd0=strdup(cmd);
    char *cmd1;
    cmd1 = strtok(cmd0, " ");
    int external = is_external_process(cmd1);
//    free ((char*) cmd1);
    free ((char*) cmd0);
    DEBUG("Is external process %d\n", external);
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigemptyset(&savintr.sa_mask);
    sigemptyset(&savequit.sa_mask);
    sigaction(SIGINT, &sa, &savintr);
    sigaction(SIGQUIT, &sa, &savequit);
    sigaddset(&sa.sa_mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &sa.sa_mask, &saveblock);
    pid = fork();
    DEBUG("fork done, pid %d\n", (int) pid);
    if (pid == 0) { //we are in the child
        sigaction(SIGINT, &savintr, (struct sigaction *)0);
        sigaction(SIGQUIT, &savequit, (struct sigaction *)0);
        sigprocmask(SIG_SETMASK, &saveblock, (sigset_t *)0);
        char *argv[4];
        argv[0] = (char*) "sh";
        argv[1] = (char*) "-c";
        argv[2] = (char*) cmd;
        argv[3] = NULL;
        if (external){ 
            DEBUG("in child, external process\n");
            execve_func_t execve_orig = dlsym(RTLD_NEXT, "execve");
            char* const *env = external_environment(environ);
            execve_orig ("/bin/sh", argv, env);
            env_free(env);
            }
        else {
            DEBUG("in child, internal appimage process\n");
            execve_func_t execvpe_orig = dlsym(RTLD_NEXT, "execvpe");
            execvpe_orig ("$APPDIR/bin/sh", argv, environ); // don't forget to embbed the shell interpreter!
            }
        _exit(127);
    }
    //we are in the parent
    if (pid == -1) {
        DEBUG("fork failed %d\n", errno);
        stat = -1; /* errno comes from fork() */
    } 
    else {
        DEBUG("in parent, waiting termination...\n");
        while (waitpid(pid, &stat, 0) == -1) {
            if (errno != EINTR){
                stat = -1;
                break;
            }
        }
    }
    sigaction(SIGINT, &savintr, (struct sigaction *)0);
    sigaction(SIGQUIT, &savequit, (struct sigaction *)0);
    sigprocmask(SIG_SETMASK, &saveblock, (sigset_t *)0);
    return(stat);
}


#ifdef EXEC_TEST
int main(int argc, char *argv[]) {
    putenv("APPIMAGE_CHECKRT_DEBUG=1");
    DEBUG("EXEC TEST\n");
    execv("./env_test", argv);

    return 0;
}
#endif
