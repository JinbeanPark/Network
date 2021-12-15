1. Jinbean Park - 805330751


2. The high-level design of your server and client

1) server.cpp

-setNonBlock()
-signHandler()

-connection()
 -receivedFile
 -recv()
 -crc.get_crc_code()
 -receivedFile.write()
 -select()

-main()
 -socket()
 -bind()
 -listen()
 -accept()
 -setNonBlock()
 -thread()


2) client.cpp

-setNonblock()
-main()
 -sendFile.open()
 -setNonblock()
 -connect()
 -select()
 -sendFile.read()
 -crc.get_crc_code()
 -send()


 3. The problems you ran into and how you solved them
 => When I made the server program, I didn't know well about the concept of non-block and select(),
 so it was confusing and taking much time to understand and apply the concepts to make the program,
 and then I was thinking about how to connect multiple clients. First way I tried was using fork(),
 but I came to realize that it is hard to interact between server and client if I use fork(), so
 I changed mind and used thread() function whenever I accept new clients. However, it was also
 taking so much time to understand and apply it to make the program. The other challenging was
 sending the u_int64_t type of CRC code. I tried using memcpy and memset to send the CRC data,
 but I was unable to resolve it, so I was thinking about how to send u_int64_t CRC code, and
 I finally decided to send CRC code by using shfit and handle each byte. Personally, I think
 this project was kind a challenging and time consuming even though I learned lots of concepts.


4. List onf any additional libraries used
#include <fcntl.h>
#include <fstream>
#include <thread>
#include <vector>
#include <sys/select.h>
#include <sys/time.h>
#include <string>
#include <assert.h>
#include <errno.h>
#include <csignal>
#include <signal.h>

5. Acknowledgement of any online tutorials or code example (except the class website) you made use of
https://stackoverflow.com/questions/6715736/using-select-for-non-blocking-sockets
https://tttsss77.tistory.com/123
https://www.ibm.com/support/knowledgecenter/en/SSLTBW_2.2.0/com.ibm.zos.v2r2.hala001/orgblockasyn.htm
https://hahaite.tistory.com/290
https://wiki.kldp.org/Translations/html/Socket_Programming-KLDP/Socket_Programming-KLDP.html#accept
https://www.it-note.kr/123
https://blockdmask.tistory.com/322
https://m.blog.naver.com/PostView.nhn?blogId=jodi999&logNo=221051811891&proxyReferer=https:%2F%2Fwww.google.com%2F
https://thispointer.com/c11-how-to-create-vector-of-thread-objects/
https://modoocode.com/192