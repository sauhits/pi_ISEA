
/**
 * prototype
 */
int exp1_tcp_listen(int port);
int exp1_tcp_connect(const char *hostname, int port);
int exp1_udp_listen(int port);
int exp1_udp_connect(const char *hostname, int port);
double gettimeofday_sec();
int exp1_do_talk(int sock);
