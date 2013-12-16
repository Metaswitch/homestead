/**
 * @file cassandracache.h class definition of a cassandra-backed cache.
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2013  Metaswitch Networks Ltd
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version, along with the "Special Exception" for use of
 * the program along with SSL, set forth below. This program is distributed
 * in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by
 * post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception
 * Metaswitch Networks Ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining OpenSSL with The
 * Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the GPL. You must comply with the GPL in all
 * respects for all of the code used other than OpenSSL.
 * "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
 * Project and licensed under the OpenSSL Licenses, or a work based on such
 * software and licensed under the OpenSSL Licenses.
 * "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
 * under which the OpenSSL Project distributes the OpenSSL toolkit software,
 * as those licenses appear in the file LICENSE-OPENSSL.
 */

#include <cache.h>
#include <unistd.h>
#include <semaphore.h>

sem_t sem;

using namespace std;

template <typename T>
class ExampleRequest : public T
{
public:
  ExampleRequest(std::string& arg1) :
    T(arg1)
  {}

  virtual ~ExampleRequest() {}

  void on_failure(Cache::ResultCode rc, std::string& text)
  {
    cout << "Request failed\n  Result: " << rc <<"\n  Text: " << text << endl;
    sem_post(&sem);
  }

  void on_success(std::string& xml)
  {
    cout << "Request succeeded:\n  XML:" << xml << endl;
    sem_post(&sem);
  }

private:
  pthread_mutex_t _lock;
};


int main(int argc, char *argv[])
{
  sem_init(&sem, 0, 0);

  cout << "------------ Cache Test ---------------" << endl;
  Cache* cache = Cache::get_instance();

  std::string alice_pub = "sip:alice@example.com";
  std::string alice_priv = "alice@example.com";

  cache->initialize();
  cache->configure("localhost", 9160, 1);
  Cache::ResultCode rc = cache->start();
  cout << "Start return code is " << rc << endl;

  ExampleRequest<Cache::GetIMSSubscription>* req1 =
    new ExampleRequest<Cache::GetIMSSubscription>(alice_pub);
  cache->send(req1);
  sem_wait(&sem);

  cache->stop();
  cache->wait_stopped();

  cout << "------------ Test Complete ---------------" << endl;
  return 0;
}
