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
 * 
 * Its purpose is to restore a suitable environment for processes that are called 
 * outside of the AppImage.
 * 
 * IMPORTANT NOTE: using this library may not be needed to solve your problem! see 
 * https://github.com/slowphil/execso/blob/master/README.md
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
  snprintf(cmd, sizeof(cmd), "command -v %s", filename); //works better

  FILE *fp;
  char *path = calloc(PATH_MAX , sizeof(char));

  /* Open the command for reading. */
  fp = popen(cmd, "r");
  if (fp == NULL) {
    //DEBUG("Failed to run command -v\n" );
    return filename;
    }
  else {
    /* Read the output a line at a time - output it. */
    char* ans = fgets(path, PATH_MAX, fp);
    pclose(fp);
      
    if (ans != NULL) {
      //DEBUG("command -v result %s", path);
      return path;
      }
    else {
      free(path);
      return filename;
      }  
    }
}

static int is_external_process(const char *filename) {
    const char *appdir = getenv("APPDIR");
    if (!appdir)
        return 0;
    //DEBUG("APPDIR = %s\n", appdir);
    const char *fullpath = canonicalize_file_name(filename);
    //DEBUG("command %s, canonical path %s\n", filename, fullpath);
    if (!fullpath) {
         fullpath = resolve_in_path (filename);
         //DEBUG("fullpath from resolve_in_path %s\n", fullpath);
         }

    int ret = strncmp(fullpath, appdir, MIN(strlen(fullpath), strlen(appdir)));
    if (fullpath != filename)
      free((char*)fullpath);
    return !(ret == 0);  
}

char* const* external_environment(char* const envp[]) {
    //DEBUG("External process detected. Restoring env vars.\n");
    //DEBUG("current process PID: %d\n", getpid());

    const char *RESTORE_ENV_PREFIX = getenv(APPIMAGE_PRESERVE_ENV_PREFIX);
    int envc = env_len(envp);
    int transfers = 0 ;
    int i;
    char** newenv = NULL;
    char* const* gpenv = NULL;
    char** tmpenv = NULL;

    if (!RESTORE_ENV_PREFIX) {
        //DEBUG("Environment prefix to pass to child not defined in %s\n", APPIMAGE_PRESERVE_ENV_PREFIX);
        //simply cleanup environment of APPDIR, LD_PRELOAD and LD_LIBRARY_PATH
        //(we assume they were not set before starting the appimage)
        newenv = env_allocate(envc); 
        for ( i = 0; i < envc; i++ ) {
            char* line = envp[i];
            if (( strncmp(line, "LD_PRELOAD", 10) ) \
                && ( strncmp(line, "LD_LIBRARY_PATH", 15))  \
                && ( strncmp(line, "APPDIR", 6)) \
                ) { 
              newenv[transfers]= strdup(envp[i]);
              transfers++;
              }
            //else DEBUG("Removing from env : %s\n", line);
            }
        //DEBUG("Found %d env variables to pass\n", transfers);    
        newenv[transfers]=0;
        }
    else {
        int prefix_len = strlen(RESTORE_ENV_PREFIX);
        tmpenv = env_allocate(envc); 
        for ( i = 0; i < envc; i++ ) {
            char* line = envp[i];
            if ( !strncmp(line, RESTORE_ENV_PREFIX, prefix_len) ) { //an environment variable starts with the keyword, we should transfer it
              ////DEBUG("Found prefixed env variables : %s\n", line);
              tmpenv[transfers]= envp[i];
              transfers++;
              }
            else if ( !strncmp(line, "PATH=", 5) ) {
              //DEBUG("saving PATH : %s\n", line);
              tmpenv[transfers]= envp[i];
              transfers++;
              }
            }
        //DEBUG("Found %d env variables to pass\n", transfers);    
        
        //now copy grandparent env and add to it the env variable we want to transfer
        gpenv = read_env_recursive();
        int gpenvc = env_len(gpenv);
        newenv = env_allocate(gpenvc + transfers); //larger new env
        // copy grandparent env in new env
        int j = 0;
        for ( i = 0; i < gpenvc ; i++ ) {
            if ( strncmp(gpenv[i], "PATH=", 5) != 0 ) {//do not copy PATH, we have it already
               newenv[j] = strdup(gpenv[i]);
               j++;
               }
            }
        //DEBUG("newenv: %zu\n", env_len(newenv));   
            
        if (transfers > 0) {
            for ( i = 0; i < transfers; i++ ) {
                newenv[j+i]= strdup(tmpenv[i]);
                //DEBUG("copying : %s\n", tmpenv[i]);
                }
            }
        DEBUG("oldenv: %zu ; tmpenv: %zu ; gpenv: %zu ; newenv: %zu\n", env_len(envp), env_len(tmpenv), env_len(gpenv), env_len(newenv));   
        newenv[j+transfers]=0;
        env_free(gpenv);
        free(tmpenv);
    }
    DEBUG("return: %zu \n", env_len(newenv));
    return newenv;
}

int execve(const char *filename, char *const argv[], char *const envp[]) {
    DEBUG("execv(e) call hijacked: %s\n", filename);
    const char *realcmd;
    if (!strncmp(filename, "/bin/sh", 7) && !strncmp(argv[0] , "sh",2) && !strncmp(argv[1] , "-c",2)){
      realcmd =  argv[2];
      }
    else {
      realcmd = filename;
      }
      
    int external = is_external_process(realcmd);
    DEBUG("COMMAND %s REALCOMMAND %s EXTERNAL %d \n", filename, realcmd, external);  
    int ret;
    execve_func_t execve_orig = dlsym(RTLD_NEXT, "execve");
    if (external){ 
        DEBUG("external process passing to execve %s\n", filename);
        char* const *env = external_environment(envp);
        ret = execve_orig (filename, argv, env);
        if (env && (env != envp)) env_free(env);
        }
    else {
        if (realcmd == filename) {
          DEBUG("internal appimage process passing to execve %s\n", filename);
          ret = execve_orig (filename, argv, envp);
          }
        else {
          const char *appdir=getenv("APPDIR");
          char buf[strlen(appdir)+8];
          snprintf(buf, sizeof buf, "%s%s", appdir, "/bin/sh");
          DEBUG("internal appimage process passing to execve %s\n", buf);
          ret = execve_orig (buf, argv, envp);
          }
        }
    return ret;
}


int execv(const char *filename, char *const argv[]) {
    return execve (filename, argv, environ);
    }


int execvpe(const char *filename, char *const argv[], char *const envp[]) {
    DEBUG("execvp(e) call hijacked: %s\n", filename);
    const char *realcmd;
    if (!strncmp(filename, "/bin/sh", 7) && !strncmp(argv[0] , "sh",2) && !strncmp(argv[1] , "-c",2)){
      realcmd =  argv[2];
      }
    else {
      realcmd = filename;
      }
    int external = is_external_process(realcmd);
    DEBUG("COMMAND %s REALCOMMAND %s EXTERNAL %d \n", filename, realcmd, external);  
    int ret;
    execve_func_t execvpe_orig = dlsym(RTLD_NEXT, "execvpe");
    if (external){ 
        DEBUG("external appimage process passing to execve %s\n", filename);
        char* const *env = external_environment(envp);
        ret = execvpe_orig (filename, argv, env);
        if (env && (env != envp)) env_free(env);
        }
    else {
        if (realcmd == filename) {
          DEBUG("internal appimage process passing to execvpe %s\n", filename);
          ret = execvpe_orig (filename, argv, envp);
          }
        else {
          const char *appdir=getenv("APPDIR");
          char buf[strlen(appdir)+8];
          snprintf(buf, sizeof buf, "%s%s", appdir, "/bin/sh");
          DEBUG("internal appimage process passing to execvpe %s\n", buf);
          ret = execvpe_orig (buf , argv, envp);
          }
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
    const char *realcmd;
    if (!strncmp(filename, "/bin/sh", 7) && !strncmp(argv[0], "sh", 2) && !strncmp(argv[1], "-c", 2)){
      realcmd =  argv[2];
      }
    else {
      realcmd = filename;
      }
      
    int external = is_external_process(realcmd);
    int ret;
    if (external){
        char* const *env = external_environment(envp);
        DEBUG("external appimage process passing to spawnp %s\n", filename);
        ret = posix_spawnp_orig(pid, filename, file_actions, attrp, argv, env);
        if (env && (env != envp)) env_free(env); 
        }
    else {
        if (realcmd == filename) {
          DEBUG("internal appimage process passing to spawnp %s\n", filename);
          ret = posix_spawnp_orig(pid, filename, file_actions, attrp, argv, envp);
          }
        else {
          const char *appdir=getenv("APPDIR");
          char buf[strlen(appdir)+8];
          snprintf(buf, sizeof buf, "%s%s", appdir, "/bin/sh");
          DEBUG("internal appimage process passing to spawnp %s\n", buf);
          ret = posix_spawnp_orig(pid, buf, file_actions, attrp, argv, envp);
          }

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
            if (env && (env != environ)) env_free(env);
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
    putenv("APPIMAGE_EXECSO_DEBUG=1");
    DEBUG("EXEC TEST\n");
    execv("./env_test", argv);

    return 0;
}
#endif
