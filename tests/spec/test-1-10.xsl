<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:template match="*" priority="10">
    <output>
      <xsl:value-of select="."/>
    </output>
  </xsl:template>
</xsl:stylesheet>
