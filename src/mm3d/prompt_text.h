// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <string>
#include <string_view>
namespace MM3D::PromptText {
struct Entry {std::string_view english,french,spanish;};
inline constexpr std::array Entries{
    Entry{"Change tab","Changer d'onglet","Cambiar pestaña"},
    Entry{"Select","Choisir","Elegir"},
    Entry{"Choose","Choisir","Elegir"}, Entry{"Return","Retour","Volver"},
    Entry{"Equip to I","Assigner I","Asignar a I"}, Entry{"Equip to II","Assigner II","Asignar a II"},
    Entry{"Items","Inventaire","Inventario"}, Entry{"Map","Carte","Mapa"},
    Entry{"Zoom in","Zoom +","Acercar"}, Entry{"Zoom out","Zoom -","Alejar"},
    Entry{"View schedule","Voir horaires","Ver horarios"}, Entry{"View events","Voir l'historique","Ver eventos"},
    Entry{"View map","Carte","Mapa"}, Entry{"Set alarm","Alarme","Alarma"},
    Entry{"Schedule details","Détails horaires","Detalles horario"},
    Entry{"Event details","Détails","Detalles suceso"},
    Entry{"Songs","Chants","Canciones"}, Entry{"Cancel","Annuler","Cancelar"},
    Entry{"Move map","Déplacer la carte","Mover mapa"},
    Entry{"Back","Retour","Volver"}, Entry{"Move cursor","Déplacer","Mover cursor"},
};
inline std::string_view Translate(unsigned language,std::string_view english) {
    for(const auto& e:Entries)if(e.english==english)
        return language==4?e.french:language==8?e.spanish:e.english;
    return english;
}
// Read the active message archive, not a requested language awaiting restart.
template<class M> unsigned Language(M& m) {
    const auto base=m.Read(0x6b373c);
    if(!base||*base<0x100000||*base>0x3fffffef||m.Read(*base)!=0x47534d47||m.Read(*base+4)!=1)return 2;
    return m.Read(*base+12).value_or(2);
}
template<class M> std::string_view Get(M& m,std::string_view english) {return Translate(Language(m),english);}
// Prompt literals are UTF-8; font lookup takes Unicode code points.
inline std::u32string Codes(std::string_view text) {
    std::u32string out;
    for(size_t i=0;i<text.size();) {
        unsigned c=static_cast<unsigned char>(text[i++]),n=0,min=0;
        if(c<0x80){}else if((c&0xe0)==0xc0){c&=31;n=1;min=0x80;}
        else if((c&0xf0)==0xe0){c&=15;n=2;min=0x800;}
        else if((c&0xf8)==0xf0){c&=7;n=3;min=0x10000;}else return {};
        if(i+n>text.size())return {};
        while(n--) {unsigned b=static_cast<unsigned char>(text[i++]);if((b&0xc0)!=0x80)return {};c=(c<<6)|(b&63);}
        if(c<min||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return {};
        out.push_back(c);
    }
    return out;
}
}
