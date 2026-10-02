#include "unit_list.h"
#include "session.h"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <vector>
static void require(bool ok){if(!ok)throw std::runtime_error("Unit list test failed");}
// A doubly linked list like SCR's active unit list: node ids 1..n, 0 is null.
struct Node {uint32_t prev=0,next=0;uint8_t owner=0;};
struct List {
  std::vector<Node> nodes;                       // index 0 unused
  explicit List(const std::vector<uint8_t>& owners):nodes(owners.size()+1) {
    for(uint32_t i=1;i<=owners.size();++i) {
      nodes[i].owner=owners[i-1];nodes[i].prev=i>1?i-1:0;nodes[i].next=i<owners.size()?i+1:0;
    }
  }
  // Mirrors LegacyView: heads and both links skip hidden owners.
  std::vector<std::array<uint32_t,2>> relink(const AllyMask& mask,uint32_t& head) const {
    const auto hidden=[&](uint32_t id){return mask.hides(nodes[id].owner);};
    std::vector<std::array<uint32_t,2>> links(nodes.size());
    head=skip_hidden_links(nodes.size()>1?1:0,64,hidden,[&](uint32_t id){return nodes[id].next;});
    for(uint32_t id=1;id<nodes.size();++id) {
      if(hidden(id))continue;
      links[id]={skip_hidden_links(nodes[id].prev,64,hidden,[&](uint32_t n){return nodes[n].prev;}),
                 skip_hidden_links(nodes[id].next,64,hidden,[&](uint32_t n){return nodes[n].next;})};
    }
    return links;
  }
};
static std::vector<uint32_t> walk(const std::vector<std::array<uint32_t,2>>& links,uint32_t head) {
  std::vector<uint32_t> order;
  for(uint32_t id=head;id && order.size()<64;id=links[id][1])order.push_back(id);
  return order;
}
int main(){
  // Pluto = slot 0. Allies = slots 1 and 2. Enemy = slot 3. Neutral = slot 11.
  Session s;s.self=0;for(unsigned i=0;i<4;++i)s.players[i]=2;
  s.alliances[0][1]=s.alliances[1][0]=s.alliances[0][2]=s.alliances[2][0]=1;
  const AllyMask mask=make_ally_mask(s);
  uint32_t head=0;

  // owners:      1  2  3  4  5  6  7  8
  List mixed({0,1,2,3,1,0,11,2});
  auto links=mixed.relink(mask,head);
  // Visible nodes are 1 (self), 4 (enemy), 6 (self), 7 (neutral). Allies 2,3,5,8 vanish.
  require((walk(links,head)==std::vector<uint32_t>{1,4,6,7}));
  require(links[4][0]==1 && links[4][1]==6);        // prev/next skip the hidden run
  require(links[6][0]==4 && links[6][1]==7);        // single hidden ally in between
  require(links[7][0]==6 && links[7][1]==0);        // trailing hidden ally ends the list
  require(links[1][0]==0);                           // head has no predecessor

  // The list head itself may be hidden.
  List ally_first({1,1,0,3});
  links=ally_first.relink(mask,head);
  require(head==3 && (walk(links,head)==std::vector<uint32_t>{3,4}));
  require(links[3][0]==0);                           // hidden predecessors collapse to null

  // Everything hidden -> empty list.
  List only_allies({1,2,1});
  links=only_allies.relink(mask,head);
  require(head==0);

  // Nobody hidden -> identical to the raw list (default games are unaffected).
  const AllyMask none;
  List plain({0,3,3,11,0});
  links=plain.relink(none,head);
  require((walk(links,head)==std::vector<uint32_t>{1,2,3,4,5}));
  for(uint32_t id=1;id<=5;++id)require(links[id][0]==plain.nodes[id].prev && links[id][1]==plain.nodes[id].next);

  // Empty list and a null link.
  require(skip_hidden_links(0,8,[](uint32_t){return true;},[](uint32_t){return 0u;})==0);

  // A corrupt cyclic list of hidden units terminates instead of hanging.
  require(skip_hidden_links(1,8,[](uint32_t){return true;},[](uint32_t id){return id==1?2u:1u;})==0);

  // Visible units are returned immediately even if later links are cyclic.
  require(skip_hidden_links(5,8,[](uint32_t){return false;},[](uint32_t id){return id;})==5);

  // Mask updates mid-game: a unit that was an ally becomes visible once the alliance ends.
  s.alliances[0][1]=s.alliances[1][0]=0;
  links=mixed.relink(make_ally_mask(s),head);
  require((walk(links,head)==std::vector<uint32_t>{1,2,4,5,6,7})); // owner 1 (nodes 2,5) visible again; owner 2 (nodes 3,8) still hidden
  std::puts("unit list relinking: hidden runs, hidden head, all hidden, no hidden, cycles and alliance changes passed");
}
