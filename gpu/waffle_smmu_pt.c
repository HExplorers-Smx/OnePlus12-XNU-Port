/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "waffle_smmu_pt.h"
#include <string.h>

#define WA_PA_LIMIT (UINT64_C(1)<<36)
#define WA_PA_MASK (WA_PA_LIMIT-WA_PT_BYTES)
#define WA_PT_TABLE UINT64_C(3)
#define WA_PT_AF (UINT64_C(1)<<10)
#define WA_PT_OUTER_SHARE (UINT64_C(2)<<8)
#define WA_PT_AP_RO (UINT64_C(2)<<6)
#define WA_PT_PXN (UINT64_C(1)<<53)
#define WA_PT_UXN (UINT64_C(1)<<54)
#define WA_PT_LEAF_BASE (WA_PT_TABLE|WA_PT_AF|WA_PT_OUTER_SHARE|WA_PT_PXN|WA_PT_UXN)

static int pa_valid(uint64_t pa) {
  return pa && !(pa&(WA_PT_BYTES-1)) && pa<=WA_PA_LIMIT-WA_PT_BYTES;
}
static int index_for(const struct wa_pt_pool *p,uint64_t pa) {
  size_t i;
  for(i=0;i<p->used;i++)if(p->pages[i].pa==pa)return (int)i;
  return -1;
}
static int table_index(const struct wa_pt_pool *p,uint64_t d) {
  if((d&3u)!=WA_PT_TABLE || (d&~(WA_PA_MASK|WA_PT_TABLE)))return -1;
  return index_for(p,d&WA_PA_MASK);
}
static int corrupt(struct wa_pt_pool *p) {p->quarantined=1;return WA_PT_CORRUPT;}
int wa_pt_init(struct wa_pt_pool *p,struct wa_pt_page *pages,size_t count) {
  size_t i,j;
  if(!p || !pages || count<1 || count>WA_PT_MAX_PAGES)return WA_PT_ARGUMENT;
  for(i=0;i<count;i++) {
    if(!pa_valid(pages[i].pa) || !pages[i].entries ||
       ((uintptr_t)pages[i].entries&(WA_PT_BYTES-1)))return WA_PT_ARGUMENT;
    for(j=0;j<i;j++)
      if(pages[i].pa==pages[j].pa || pages[i].entries==pages[j].entries)
        return WA_PT_ARGUMENT;
  }
  memset(p,0,sizeof(*p));p->pages=pages;p->capacity=count;p->used=1;
  for(i=0;i<count;i++)memset(pages[i].entries,0,WA_PT_BYTES);
  return WA_PT_OK;
}
int wa_pt_preflight_page(const struct wa_pt_pool *p,uint64_t iova,uint64_t pa,
                         enum wa_pt_access access) {
  uint64_t first,second,third;
  int j,k;size_t needed=0;
  if(!p || !p->pages || !p->used || p->used>p->capacity ||
     p->capacity>WA_PT_MAX_PAGES || p->quarantined)return WA_PT_CORRUPT;
  if(p->sealed)return WA_PT_FROZEN;
  if(iova>UINT32_MAX || (iova&4095u) || !pa_valid(pa) ||
     (access!=WA_PT_READONLY && access!=WA_PT_READWRITE))return WA_PT_ARGUMENT;
  first=p->pages[0].entries[(iova>>30)&3u];
  if(!first)needed=2;
  else {
    j=table_index(p,first);if(j<=0)return WA_PT_CORRUPT;
    second=p->pages[j].entries[(iova>>21)&511u];
    if(!second)needed=1;
    else {
      k=table_index(p,second);if(k<=0)return WA_PT_CORRUPT;
      third=p->pages[k].entries[(iova>>12)&511u];
      if(third)return (third&3u)==3u?WA_PT_CONFLICT:WA_PT_CORRUPT;
    }
  }
  return p->capacity-p->used<needed?WA_PT_NO_PAGES:WA_PT_OK;
}
int wa_pt_map_page(struct wa_pt_pool *p,uint64_t iova,uint64_t pa,
                   enum wa_pt_access access) {
  uint32_t l1,l2,l3;
  uint64_t d1,d2,d3,attr;
  int j=-1,k=-1,new_l2=0,new_l3=0,rc;
  if((rc=wa_pt_preflight_page(p,iova,pa,access))!=WA_PT_OK) return rc;
  if(!p || !p->pages)return WA_PT_ARGUMENT;
  if(p->quarantined)return WA_PT_CORRUPT;
  if(p->sealed)return WA_PT_FROZEN;
  if(iova>UINT32_MAX || (iova&4095u) || !pa_valid(pa) ||
      (access!=WA_PT_READONLY && access!=WA_PT_READWRITE))return WA_PT_ARGUMENT;
  l1=(uint32_t)(iova>>30)&3u;l2=(uint32_t)(iova>>21)&511u;
  l3=(uint32_t)(iova>>12)&511u;
  d1=p->pages[0].entries[l1];
  if(d1==0)new_l2=1;
  else if((j=table_index(p,d1))<=0)return corrupt(p);
  if(!new_l2) {
    d2=p->pages[j].entries[l2];
    if(d2==0)new_l3=1;
    else if((k=table_index(p,d2))<=0)return corrupt(p);
    if(!new_l3 && p->pages[k].entries[l3]) {
      d3=p->pages[k].entries[l3];
      if((d3&3u)!=3u || (d3&~(WA_PA_MASK|WA_PT_LEAF_BASE|WA_PT_AP_RO)))
        return corrupt(p);
      return WA_PT_CONFLICT;
    }
  } else new_l3=1;
  if(p->capacity-p->used<(size_t)(new_l2+new_l3))return WA_PT_NO_PAGES;
  /* All failure checks precede descriptor edits: map_page is atomic in
   * host RAM. It is forbidden after the first publication/seal. */
  if(new_l2) {
    j=(int)p->used++;
    p->pages[0].entries[l1]=p->pages[j].pa|WA_PT_TABLE;
  }
  if(new_l3) {
    k=(int)p->used++;
    p->pages[j].entries[l2]=p->pages[k].pa|WA_PT_TABLE;
  }
  attr=WA_PT_LEAF_BASE|(access==WA_PT_READONLY?WA_PT_AP_RO:0);
  p->pages[k].entries[l3]=pa|attr;
  p->mappings++;
  return WA_PT_OK;
}
int wa_pt_lookup(const struct wa_pt_pool *p,uint64_t iova,
                 uint64_t *pa,enum wa_pt_access *access) {
  uint64_t a,b,c;
  int j,k;
  if(!p || !p->pages || p->quarantined || iova>UINT32_MAX || !pa || !access)
    return WA_PT_ARGUMENT;
  a=p->pages[0].entries[(iova>>30)&3u];
  if(!a)return WA_PT_MISSING;
  j=table_index(p,a);if(j<=0)return WA_PT_CORRUPT;
  b=p->pages[j].entries[(iova>>21)&511u];
  if(!b)return WA_PT_MISSING;
  k=table_index(p,b);if(k<=0)return WA_PT_CORRUPT;
  c=p->pages[k].entries[(iova>>12)&511u];
  if(!c)return WA_PT_MISSING;
  if((c&3u)!=3u || (c&~(WA_PA_MASK|WA_PT_LEAF_BASE|WA_PT_AP_RO)) ||
     (c&WA_PT_LEAF_BASE)!=WA_PT_LEAF_BASE)return WA_PT_CORRUPT;
  *pa=(c&WA_PA_MASK)|(iova&4095u);
  *access=(c&WA_PT_AP_RO)?WA_PT_READONLY:WA_PT_READWRITE;
  return WA_PT_OK;
}
int wa_pt_audit(const struct wa_pt_pool *p,size_t *mappings) {
  uint8_t seen[WA_PT_MAX_PAGES]={0};
  size_t count=0,a,b,c;
  if(mappings)*mappings=0;
  if(!p || !p->pages || !p->used || p->used>p->capacity || p->quarantined)
    return WA_PT_CORRUPT;
  seen[0]=1;
  for(a=0;a<WA_PT_ENTRIES;a++) {
    uint64_t first=p->pages[0].entries[a];int j;
    if(!first)continue;
    if(a>=4 || (j=table_index(p,first))<=0 || seen[j])return WA_PT_CORRUPT;
    seen[j]=1;
    for(b=0;b<WA_PT_ENTRIES;b++) {
      uint64_t second=p->pages[j].entries[b];int k;
      if(!second)continue;
      if((k=table_index(p,second))<=0 || seen[k])return WA_PT_CORRUPT;
      seen[k]=1;
      for(c=0;c<WA_PT_ENTRIES;c++) {
        uint64_t leaf=p->pages[k].entries[c];
        if(!leaf)continue;
        if(!pa_valid(leaf&WA_PA_MASK) || (leaf&3u)!=3u ||
           (leaf&WA_PT_LEAF_BASE)!=WA_PT_LEAF_BASE ||
           (leaf&~(WA_PA_MASK|WA_PT_LEAF_BASE|WA_PT_AP_RO)))return WA_PT_CORRUPT;
        count++;
      }
    }
  }
  for(a=0;a<p->used;a++)if(!seen[a])return WA_PT_CORRUPT;
  if(count!=p->mappings)return WA_PT_CORRUPT;
  if(mappings)*mappings=count;
  return WA_PT_OK;
}
int wa_pt_seal(struct wa_pt_pool *p,uint64_t *root_pa) {
  if(!p || !root_pa || p->sealed)return WA_PT_ARGUMENT;
  if(wa_pt_audit(p,0)!=WA_PT_OK)return corrupt(p);
  p->sealed=1;*root_pa=p->pages[0].pa;
  return WA_PT_OK;
}
