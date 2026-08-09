#pragma once

// Hardcoded config example
inline const char* XTEMPLATE_CONTENT_HARDCODED = R"(
hardcoded conditional stdout
TAGS: hardcoded, cout, reference
VARS: const std::string &; level, const std::string &[REQUIRED]; title, const std::string &; desc, XCHECKBOX; highlight_level
<TEMPLATE_BODY>
##IF_NEMPTY level
  ##IF_NEMPTY desc
    ##IF_ON highlight_level
std::cout << "!!![$level]!!! $title: $desc" << std::endl;
    ##ELSE
std::cout << "[$level] $title: $desc" << std::endl;
    ##END
  ##ELSE
    ##IF_ON highlight_level
std::cout << "!!![$level]!!! $title" << std::endl;
    ##ELSE
std::cout << "[$level] $title" << std::endl;
    ##END
  ##END
##ELSE
  ##IF_NEMPTY desc
std::cout << "$title: $desc" << std::endl;
  ##ELSE
std::cout << "$title" << std::endl;
  ##END
##END
</TEMPLATE_BODY>

hardcoded conditional recursive stdout
TAGS: hardcoded, conditional, reference
VARS: XCHECKBOX; use_level, XVARIANT:warn-error=use_level; level, const std::string &[REQUIRED]; title, const std::string &; desc
<TEMPLATE_BODY>
##IF_OFF use_level
   ##CLEAR level
##END
##IF_ON use_level
  ##IF_EQ level warn
    ##XTEMPLATE 'hardcoded conditional stdout' --level $level --title $title --desc $desc
  ##ELIF_EQ level error
    ##XTEMPLATE 'hardcoded conditional stdout' --level $level --title $title --desc $desc --highlight_level ON
  ##ELSE
    ##ERROR level = $level is unexpected (should be warn or error)
  ##END
##ELSE
  ##XTEMPLATE 'hardcoded conditional stdout' --title $title --desc $desc
##END
</TEMPLATE_BODY>
)";
