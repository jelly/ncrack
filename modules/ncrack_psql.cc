/***************************************************************************
 * ncrack_psql.cc -- ncrack module for the PSQL protocol                   *
 *                                                                         *
 ***********************IMPORTANT NMAP LICENSE TERMS************************
 *                                                                         *
 * The Nmap Security Scanner is (C) 1996-2011 Insecure.Com LLC. Nmap is    *
 * also a registered trademark of Insecure.Com LLC.  This program is free  *
 * software; you may redistribute and/or modify it under the terms of the  *
 * GNU General Public License as published by the Free Software            *
 * Foundation; Version 2 with the clarifications and exceptions described  *
 * below.  This guarantees your right to use, modify, and redistribute     *
 * this software under certain conditions.  If you wish to embed Nmap      *
 * technology into proprietary software, we sell alternative licenses      *
 * (contact sales@insecure.com).  Dozens of software vendors already       *
 * license Nmap technology such as host discovery, port scanning, OS       *
 * detection, and version detection.                                       *
 *                                                                         *
 * Note that the GPL places important restrictions on "derived works", yet *
 * it does not provide a detailed definition of that term.  To avoid       *
 * misunderstandings, we consider an application to constitute a           *
 * "derivative work" for the purpose of this license if it does any of the *
 * following:                                                              *
 * o Integrates source code from Nmap                                      *
 * o Reads or includes Nmap copyrighted data files, such as                *
 *   nmap-os-db or nmap-service-probes.                                    *
 * o Executes Nmap and parses the results (as opposed to typical shell or  *
 *   execution-menu apps, which simply display raw Nmap output and so are  *
 *   not derivative works.)                                                *
 * o Integrates/includes/aggregates Nmap into a proprietary executable     *
 *   installer, such as those produced by InstallShield.                   *
 * o Links to a library or executes a program that does any of the above   *
 *                                                                         *
 * The term "Nmap" should be taken to also include any portions or derived *
 * works of Nmap.  This list is not exclusive, but is meant to clarify our *
 * interpretation of derived works with some common examples.  Our         *
 * interpretation applies only to Nmap--we don't speak for other people's  *
 * GPL works.                                                              *
 *                                                                         *
 * If you have any questions about the GPL licensing restrictions on using *
 * Nmap in non-GPL works, we would be happy to help.  As mentioned above,  *
 * we also offer alternative license to integrate Nmap into proprietary    *
 * applications and appliances.  These contracts have been sold to dozens  *
 * of software vendors, and generally include a perpetual license as well  *
 * as providing for priority support and updates as well as helping to     *
 * fund the continued development of Nmap technology.  Please email        *
 * sales@insecure.com for further information.                             *
 *                                                                         *
 * As a special exception to the GPL terms, Insecure.Com LLC grants        *
 * permission to link the code of this program with any version of the     *
 * OpenSSL library which is distributed under a license identical to that  *
 * listed in the included docs/licenses/OpenSSL.txt file, and distribute   *
 * linked combinations including the two. You must obey the GNU GPL in all *
 * respects for all of the code used other than OpenSSL.  If you modify    *
 * this file, you may extend this exception to your version of the file,   *
 * but you are not obligated to do so.                                     *
 *                                                                         *
 * If you received these files with a written license agreement or         *
 * contract stating terms other than the terms above, then that            *
 * alternative license agreement takes precedence over these comments.     *
 *                                                                         *
 * Source is provided to this software because we believe users have a     *
 * right to know exactly what a program is going to do before they run it. *
 * This also allows you to audit the software for security holes (none     *
 * have been found so far).                                                *
 *                                                                         *
 * Source code also allows you to port Nmap to new platforms, fix bugs,    *
 * and add new features.  You are highly encouraged to send your changes   *
 * to nmap-dev@insecure.org for possible incorporation into the main       *
 * distribution.  By sending these changes to Fyodor or one of the         *
 * Insecure.Org development mailing lists, it is assumed that you are      *
 * offering the Nmap Project (Insecure.Com LLC) the unlimited,             *
 * non-exclusive right to reuse, modify, and relicense the code.  Nmap     *
 * will always be available Open Source, but this is important because the *
 * inability to relicense code has caused devastating problems for other   *
 * Free Software projects (such as KDE and NASM).  We also occasionally    *
 * relicense the code to third parties as discussed above.  If you wish to *
 * specify special license conditions of your contributions, just say so   *
 * when you send them.                                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful, but     *
 * WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU       *
 * General Public License v2.0 for more details at                         *
 * http://www.gnu.org/licenses/gpl-2.0.html , or in the COPYING file       *
 * included with Nmap.                                                     *
 *                                                                         *
 ***************************************************************************/


#include "ncrack.h"
#include "nsock.h"
#include "NcrackOps.h"
#include "Service.h"
#include "modules.h"
#include <openssl/md5.h>

#define PSQL_TIMEOUT 20000
#define PSQL_DIGITS 1
#define PSQL_PACKET_LENGTH 4
#define PSQL_AUTH_TYPE 4
#define PSQL_SALT 4

extern void ncrack_read_handler(nsock_pool nsp, nsock_event nse, void *mydata);
extern void ncrack_write_handler(nsock_pool nsp, nsock_event nse, void *mydata);
extern void ncrack_module_end(nsock_pool nsp, void *mydata);
static int psql_loop_read(nsock_pool nsp, Connection *con);
unsigned char charToHexDigit(char c);
enum states { PSQL_INIT, PSQL_AUTH, PSQL_FINI };



/* n is the size of src. dest must have at least n * 2 + 1 allocated bytes. */
static char *enhex(char *dest, const unsigned char *src, size_t n)
{
    unsigned int i;

    for (i = 0; i < n; i++)
        Snprintf(dest + i * 2, 3, "%02x", src[i]);

    return dest;
}


/* Arguments are assumed to be non-NULL, with the exception of nc and cnonce,
   which may be garbage only if qop == QOP_NONE. */
static void make_response(char buf[MD5_DIGEST_LENGTH * 2 + 3 + 1],
    const char *username, const char *password, const char *salt)
{
    char HA1_hex[MD5_DIGEST_LENGTH * 2 + 1], HA2_hex[MD5_DIGEST_LENGTH * 2 + 1];
    unsigned char hashbuf[MD5_DIGEST_LENGTH];
    char finalhash[MD5_DIGEST_LENGTH * 2 + 3];
    MD5_CTX md5;
    salt = "6e519fe2";
    printf("New username: %s\n",username);
    
    /* Calculate MD5(Password + Username) */
    MD5_Init(&md5);

    MD5_Update(&md5, password, strlen(password));
    MD5_Update(&md5, username, strlen(username));
    MD5_Final(hashbuf, &md5);
    enhex(HA1_hex, hashbuf, sizeof(hashbuf));
    //printf("First hash: %s", HA1_hex);
    /* Calculate response MD5(above + Salt). */
    MD5_Init(&md5);
    MD5_Update(&md5, HA1_hex, strlen(HA1_hex));
    MD5_Update(&md5, salt, strlen(salt));
    MD5_Final(hashbuf, &md5);
    enhex(buf, hashbuf, sizeof(hashbuf));
    //printf("Hash: %s \n",hashbuf);
    char md55[4] = "md5";
    //memcpy(md55,finalhash, sizeof(md55));
    //memcpy(hashbuf, finalhash, sizeof(buf));
    strcpy(finalhash,"md5");
    strcat(finalhash, buf);
    // printf("Yolo: %s \n", finalhash);
    // printf("asdad %d \n", strlen(finalhash));
    // printf("asdad %d \n", strlen(buf));
    buf[0] = '\0';
    // printf("hehe : %d\n", MD5_DIGEST_LENGTH * 2 + 3);
    strncpy(buf, finalhash, MD5_DIGEST_LENGTH * 2 + 3);
    buf[MD5_DIGEST_LENGTH * 2 + 3] = '\0';
    // printf("asdad %d \n", strlen(buf));
}

static int
psql_loop_read(nsock_pool nsp, Connection *con, char *psql_code_ret, char *psql_salt_ret)
{
  int i,j = 0;
  char psql_code[PSQL_DIGITS + 1]; /* 1 char + '\0' */
  char psql_type[2];
  char psql_packet_length[PSQL_PACKET_LENGTH + 1]; /* 4 chars + '\0' */
  char dig[2]; /* temporary digit string */
  u_char *p2;
  char *p;
  int packet_length;
  int num;  
  if (con->inbuf == NULL || con->inbuf->get_len() < PSQL_DIGITS + 1) {
    nsock_read(nsp, con->niod, ncrack_read_handler, PSQL_TIMEOUT, con);
    return -1;
  }
  /* Make sure first 5 (PSQL_DIGITS) bytes of each line are something */
  p = (char *)con->inbuf->get_dataptr();
  dig[1] = '\0';
  for (i = 0; i < PSQL_DIGITS; i++) {
    dig[0] = *p++;
    psql_code[i] = dig[0];
  }
  psql_code[1] = '\0';
  strncpy(psql_code_ret, psql_code, PSQL_DIGITS);

  if (!strncmp(psql_code_ret, "R", PSQL_DIGITS)) {
    /* Read packet length only if it is of type R */
    char array[3];
    for (i = 0; i < PSQL_PACKET_LENGTH; i++) {
     dig[0] = *p++;
     sprintf(array, "\n%x", dig[0]);
     num = (int)strtol(array, NULL, 16);
    }

      /* Currently we care only for the last byte.
       The salt packet will always be small enough 
       to fit in that byte */

    if (con->inbuf->get_len() < num + 1) {
      nsock_read(nsp, con->niod, ncrack_read_handler, PSQL_TIMEOUT, con);
      return -1;
    }
    
    /* At this point we will need to know the authentication type. 
      Possible values are 5 and 0. 5 stands for MD5 and 0 stands for successful
      authentication. If it is 5 we are at the second stage of the process PSQL_AUTH
      while if it is 0 we are at the third stage PSQL_FINI.
      This value consists of 4 bytes but only the fourth will have a value
      e.g. 00 00 00 05 for MD5 or 00 00 00 00 for successful authentication. */

    char array2[5];
    for (i = 0; i < PSQL_AUTH_TYPE; i++) {
      dig[0] = *p++;
      sprintf(array2, "\n%x", dig[0]);
      num = (int)strtol(array2, NULL, 16);
    }

    /* If authentication type is 'MD5 password' (carries Salt) read salt */
    if (num == 5) {
      printf("Authentication type: MD5");
      p2 = (u_char *)con->inbuf->get_dataptr();
      char psql_salt[PSQL_SALT*2 + 1]; /* 8 characters + null terminator*/
      char psql_salt_good[3];
      psql_type[1] = '\0';

      for(i = 0; i < con->inbuf->get_len() - 1; i++){
       *p2++;  

       /* The following proccess will omit leading 0.
        In that case we will manually add one. */

       if (i == con->inbuf->get_len() - 5){
         sprintf(psql_salt_good, "%x", *p2);
         if (strlen(psql_salt_good) < 2)
           sprintf(psql_salt_good, "0%x", *p2);
         strcpy(psql_salt,psql_salt_good);
         j++;
       }
       else if (i >= con->inbuf->get_len() - 5){
         sprintf(psql_salt_good, "%x", *p2);
         if (strlen(psql_salt_good)< 2)
           sprintf(psql_salt_good, "0%x", *p2);
         strcat(psql_salt, psql_salt_good);
         j++;
       } 
       
      }
      psql_salt[PSQL_SALT * 2] = '\0';

      printf("\nSalt: %s\n", psql_salt);
    
      strncpy(psql_salt_ret, psql_salt, PSQL_SALT*2);
      return 0;

    }
    else if (num == 0) {
      return 1;
    }

  } else if (!strncmp(psql_code_ret, "E", PSQL_DIGITS)) {

    /* Error packet. The login attempt has failed.
      Perhaps we could do some more validation on the packet.
      Currently any kind of packet with E as the first byte will
      be interpreted as a Error package. It is only a matter 
      of concerns if the service is not a Postgres service.  */
    return 0;

  }
    /* Malformed packet. Exit safely. */ 
    return -2;  
}

void
ncrack_psql(nsock_pool nsp, Connection *con)
{
  nsock_iod nsi = con->niod;
  Service *serv = con->service;
  const char *hostinfo = serv->HostInfo();

  char packet_length;
  char psql_code[PSQL_DIGITS + 1];
  char psql_salt[PSQL_SALT*2 + 1];
  
  memset(psql_code, 0, sizeof(psql_code));
  memset(psql_salt, 0, sizeof(psql_salt));
   
  char response_hex[MD5_DIGEST_LENGTH *2 + 3];
  switch (con->state)
  {
    case PSQL_INIT:
     
      con->state = PSQL_AUTH;
      delete con->inbuf;
      con->inbuf = NULL;

      if (con->outbuf)
        delete con->outbuf;

      con->outbuf = new Buf();
      packet_length = strlen(con->user) + 7 +
          strlen("\x03user  database postgres application_name psql client_encoding UTF8  ");

      printf("Username: %s\n", con->user);
      printf("Password: %s\n", con->pass);

      con->outbuf->snprintf(packet_length,
           "%c%c%c%c%c\x03%c%cuser%c%s%cdatabase%cpostgres%capplication_name%cpsql%cclient_encoding%cUTF8%c%c", 
           0,0,0,packet_length,0,0,0,0,con->user,0,0,0,0,0,0,0,0);
      nsock_write(nsp, nsi, ncrack_write_handler, PSQL_TIMEOUT, con,
        (const char *)con->outbuf->get_dataptr(), con->outbuf->get_len());

      break;

    case PSQL_AUTH:

      if (psql_loop_read(nsp, con, psql_code, psql_salt) < 0)
        break;

      
      make_response(response_hex, con->user + strlen("md5") , con->pass, psql_salt);
      printf("%s\n",response_hex);

      response_hex[MD5_DIGEST_LENGTH * 2 + 3] = '\0';
      
      con->state = PSQL_FINI;
      delete con->inbuf;
      con->inbuf = NULL;


      if (con->outbuf)
        delete con->outbuf;

      con->outbuf = new Buf();
      packet_length = strlen(response_hex) + 5 + strlen("p");

      /* This packet will not count the last null byte in packet length byte,
        that is why we remove one from snprintf. */
      con->outbuf->snprintf(packet_length,
            "p%c%c%c%c%s%c",0,0,0,packet_length - 1,response_hex,0
        );

      nsock_write(nsp, nsi, ncrack_write_handler, PSQL_TIMEOUT, con,
        (const char *)con->outbuf->get_dataptr(), con->outbuf->get_len());
      break;

    case PSQL_FINI:

      if (psql_loop_read(nsp, con, psql_code, psql_salt) < 0)
        break;
      else if (psql_loop_read(nsp, con , psql_code, psql_salt) == 1)
        con->auth_success = true; 

      con->state = PSQL_INIT;

      delete con->inbuf;
      con->inbuf = NULL;

      return ncrack_module_end(nsp, con);
  }
  /* make sure that ncrack_module_end() is always called last or returned to
   * have tail recursion or else stack space overflow might occur */
}
